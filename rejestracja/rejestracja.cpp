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

static bool is_valid_department(UrzednikRole department) {
    switch (department) {
        case UrzednikRole::SC:
        case UrzednikRole::KM:
        case UrzednikRole::ML:
        case UrzednikRole::PD:
        case UrzednikRole::SA:
            return true;
        default:
            return false;
    }
}

int rejestracja_main() {
    ipc::install_signal_handler(SIGTERM, handle_shutdown_signal);
    ipc::install_signal_handler(SIGUSR1, handle_finish_signal);
    signal(SIGINT, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    Logger::log(LogSeverity::Info, Identity::Rejestracja, "Rejestracja uruchomiona.");

    auto shared_state = ipc::helper::get_shared_state(false);
    if (!shared_state) {
        return 1;
    }

    int msg_req_id = ipc::helper::get_msg_queue(ipc::KeyType::MsgQueueRejestracja);
    if (msg_req_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    int sem_id = ipc::helper::get_semaphore_set(2);
    if (sem_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    while (rejestracja_running) {
        if (stop_after_current) {
            break;
        }

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

        if (ipc::sem::wait(sem_id, 1) == -1) {
            Logger::log(LogSeverity::Err, Identity::Rejestracja, "Blad blokady stanu wspoldzielonego.");
        }
        else {
            if (shared_state->current_queue_length > 0) {
                shared_state->current_queue_length--;
            }
            if (ipc::sem::post(sem_id, 1) == -1) {
                Logger::log(LogSeverity::Err, Identity::Rejestracja, "Blad odblokowania stanu wspoldzielonego.");
            }
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
            rejected.reject_reason = TicketRejectReason::OfficeClosed;
            if (ipc::msg::send<TicketIssuedMsg>(msg_req_id, static_cast<long>(request.petent_id), rejected) == -1) {
                std::string error =
                    "Blad zwrotu informacji o zamknietym urzedzie dla petenta " + std::to_string(request.petent_id);
                Logger::log(LogSeverity::Err, Identity::Rejestracja, error);
            }
            Logger::log(LogSeverity::Notice, Identity::Rejestracja, "Urzad zamkniety, bilet nie zostal wydany.");
            continue;
        }

        UrzednikRole department = request.department;
        if (!is_valid_department(department)) {
            Logger::log(LogSeverity::Err, Identity::Rejestracja, "Nieprawidlowy wydzial w prosbie o bilet.");
            department = UrzednikRole::SA;
        }
        int idx = static_cast<int>(department);
        uint32_t ticket_number = 0;
        TicketRejectReason reject_reason = TicketRejectReason::None;
        if (ipc::sem::wait(sem_id, 1) == -1) {
            Logger::log(LogSeverity::Err, Identity::Rejestracja, "Blad blokady licznika biletow.");
            continue;
        }
        uint32_t limit = shared_state->ticket_limits[idx];
        uint32_t current = shared_state->ticket_counters[idx];
        if (limit != 0 && current >= limit) {
            reject_reason = TicketRejectReason::LimitReached;
        }
        else {
            ticket_number = ++shared_state->ticket_counters[idx];
        }
        if (ipc::sem::post(sem_id, 1) == -1) {
            Logger::log(LogSeverity::Err, Identity::Rejestracja, "Blad odblokowania licznika biletow.");
        }

        if (reject_reason != TicketRejectReason::None) {
            TicketIssuedMsg rejected{};
            rejected.petent_id = request.petent_id;
            rejected.ticket_number = 0;
            rejected.department = department;
            rejected.redirected_from_sa = 0;
            rejected.reject_reason = reject_reason;
            if (ipc::msg::send<TicketIssuedMsg>(msg_req_id, static_cast<long>(request.petent_id), rejected) == -1) {
                std::string error =
                    "Blad zwrotu informacji o braku limitu dla petenta " + std::to_string(request.petent_id);
                Logger::log(LogSeverity::Err, Identity::Rejestracja, error);
            }
            auto dept_name = urzednik_role_to_string(department);
            std::string dept = dept_name ? std::string(*dept_name) : std::string("?");
            Logger::log(LogSeverity::Notice, Identity::Rejestracja,
                        "Brak wolnych terminow w wydziale " + dept + ", bilet nie zostal wydany.");
            continue;
        }

        TicketIssuedMsg issued{};
        issued.petent_id = request.petent_id;
        issued.ticket_number = ticket_number;
        issued.department = department;
        issued.redirected_from_sa = 0;
        issued.reject_reason = TicketRejectReason::None;

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
