#include "rejestracja.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include "../common.h"
#include "../ipcutils.h"
#include "../logger.h"

constexpr long kTicketRequestType = 1;
static volatile sig_atomic_t rejestracja_running = 1;
static volatile sig_atomic_t stop_after_current = 0;

static void handle_shutdown_signal(int) {
    rejestracja_running = 0;
}

static void handle_finish_signal(int) {
    stop_after_current = 1;
}

UrzednikRole choose_department() {
    int roll = rng::random_int(1, 100);
    if (roll <= 60) {
        return UrzednikRole::SA;
    }
    if (roll <= 70) {
        return UrzednikRole::SC;
    }
    if (roll <= 80) {
        return UrzednikRole::KM;
    }
    if (roll <= 90) {
        return UrzednikRole::ML;
    }
    return UrzednikRole::PD;
}

int rejestracja_main() {
    std::signal(SIGTERM, handle_shutdown_signal);
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGUSR1, handle_finish_signal);

    Logger::log(LogSeverity::Info, Identity::Rejestracja, "Rejestracja uruchomiona.");

    key_t shm_key = ipc::make_key(ipc::KeyType::SharedState);
    if (shm_key == -1) {
        return 1;
    }

    int shm_id = ipc::shm::get<SharedState>(shm_key);
    if (shm_id == -1) {
        return 1;
    }

    auto shared_state = ipc::shm::attach<SharedState>(shm_id, false);
    if (!shared_state) {
        return 1;
    }

    key_t msg_req_key = ipc::make_key(ipc::KeyType::MsgQueueRejestracja);
    if (msg_req_key == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    int msg_req_id = ipc::msg::get(msg_req_key);
    if (msg_req_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    key_t sem_key = ipc::make_key(ipc::KeyType::SemaphoreSet);
    if (sem_key == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    int sem_id = ipc::sem::get(sem_key, 2);
    if (sem_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    while (rejestracja_running) {
        TicketRequestMsg request{};
        int rc = ipc::msg::receive<TicketRequestMsg>(msg_req_id, kTicketRequestType, &request, 0);
        if (rc == -1) {
            if (errno != EINTR) {
                std::string error = "Blad odbioru z kolejki biletow: " + std::string(std::strerror(errno));
                Logger::log(LogSeverity::Err, Identity::Rejestracja, error);
            } else if (!rejestracja_running || stop_after_current) {
                break;
            }
            continue;
        }

        if (request.petent_id == 0) {
            Logger::log(LogSeverity::Notice, Identity::Rejestracja, "Otrzymano sygnal zakonczenia.");
            break;
        }

        if (shared_state->current_queue_length > 0) {
            shared_state->current_queue_length--;
        }

        if (ipc::sem::post(sem_id, 0) == -1) {
            Logger::log(LogSeverity::Err, Identity::Rejestracja, "Blad zwiekszenia liczby wolnych miejsc w kolejce.");
        }

        if (shared_state->office_status == OfficeStatus::Closed) {
            TicketIssuedMsg rejected{};
            rejected.petent_id = request.petent_id;
            rejected.ticket_number = 0;
            rejected.department = UrzednikRole::SA;
            rejected.redirected_from_sa = 0;
            if (ipc::msg::send<TicketIssuedMsg>(msg_req_id, static_cast<long>(request.petent_id), rejected) == -1) {
                std::string error =
                    "Blad zwrotu informacji o zamknietym urzedzie dla petenta " + std::to_string(request.petent_id);
                Logger::log(LogSeverity::Err, Identity::Rejestracja, error);
            }
            Logger::log(LogSeverity::Notice, Identity::Rejestracja, "Urzad zamkniety, bilet nie zostal wydany.");
            continue;
        }

        UrzednikRole department = choose_department();
        int idx = static_cast<int>(department);
        uint32_t ticket_number = 0;
        if (ipc::sem::wait(sem_id, 1) == -1) {
            Logger::log(LogSeverity::Err, Identity::Rejestracja, "Blad blokady licznika biletow.");
            continue;
        }
        ticket_number = ++shared_state->ticket_counters[idx];
        if (ipc::sem::post(sem_id, 1) == -1) {
            Logger::log(LogSeverity::Err, Identity::Rejestracja, "Blad odblokowania licznika biletow.");
        }

        TicketIssuedMsg issued{};
        issued.petent_id = request.petent_id;
        issued.ticket_number = ticket_number;
        issued.department = department;
        issued.redirected_from_sa = 0;

        if (ipc::msg::send<TicketIssuedMsg>(msg_req_id, static_cast<long>(request.petent_id), issued) == -1) {
            std::string error = "Blad wyslania biletu dla petenta " + std::to_string(request.petent_id);
            Logger::log(LogSeverity::Err, Identity::Rejestracja, error);
            continue;
        }

        Logger::log(LogSeverity::Info, Identity::Rejestracja,
                    "Wydano bilet nr " + std::to_string(ticket_number) + " do wydzialu.");

        if (stop_after_current) {
            break;
        }
    }

    ipc::shm::detach(shared_state);
    Logger::log(LogSeverity::Info, Identity::Rejestracja, "Rejestracja zakonczona.");
    return 0;
}
