#include "petent.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include <unistd.h>
#include "../ipcutils.h"
#include "../logger.h"

constexpr long kTicketRequestType = 1;
constexpr long kUrzednikQueueType = 1;
static volatile sig_atomic_t petent_evacuating = 0;

static void handle_evacuation_signal(int) { petent_evacuating = 1; }

static void log_evacuation() {
    Logger::log(LogSeverity::Notice, Identity::Petent, "Ewakuacja - petent opuszcza budynek.");
}

int petent_main(UrzednikRole department) {
    std::signal(SIGUSR2, handle_evacuation_signal);
    std::signal(SIGTERM, handle_evacuation_signal);
    std::signal(SIGINT, handle_evacuation_signal);

    Logger::log(LogSeverity::Info, Identity::Petent, "Petent uruchomiony.");

    auto shared_state = ipc::helper::get_shared_state(false);
    if (!shared_state) {
        return 1;
    }

    int sem_id = ipc::helper::get_semaphore_set(2);
    if (sem_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    int msg_req_id = ipc::helper::get_msg_queue(ipc::KeyType::MsgQueueRejestracja);
    if (msg_req_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    if (petent_evacuating) {
        log_evacuation();
        ipc::shm::detach(shared_state);
        return 0;
    }

    if (shared_state->office_status == OfficeStatus::Closed) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Urzad zamkniety - petent wychodzi.");
        ipc::shm::detach(shared_state);
        return 0;
    }

    if (ipc::sem::wait(sem_id, 0) == -1) {
        if (errno == EINTR && petent_evacuating) {
            log_evacuation();
            ipc::shm::detach(shared_state);
            return 0;
        }
        Logger::log(LogSeverity::Err, Identity::Petent, "Blad oczekiwania na miejsce w kolejce.");
        ipc::shm::detach(shared_state);
        return 1;
    }

    if (ipc::sem::wait(sem_id, 1) == -1) {
        Logger::log(LogSeverity::Err, Identity::Petent, "Blad blokady stanu wspoldzielonego.");
    }
    else {
        shared_state->current_queue_length++;
        if (ipc::sem::post(sem_id, 1) == -1) {
            Logger::log(LogSeverity::Err, Identity::Petent, "Blad odblokowania stanu wspoldzielonego.");
        }
    }

    pid_t petent_id = getpid();
    TicketRequestMsg request{};
    request.petent_id = petent_id;
    request.department = department;
    request.is_vip = rng::random_int(1, 100) <= 10 ? 1 : 0;
    request.has_child = rng::random_int(1, 100) <= 20 ? 1 : 0;

    if (ipc::msg::send<TicketRequestMsg>(msg_req_id, kTicketRequestType, request) == -1) {
        Logger::log(LogSeverity::Err, Identity::Petent, "Blad wyslania prosby o bilet.");
        if (ipc::sem::wait(sem_id, 1) == 0) {
            if (shared_state->current_queue_length > 0) {
                shared_state->current_queue_length--;
            }
            ipc::sem::post(sem_id, 1);
        }
        ipc::sem::post(sem_id, 0);
        ipc::shm::detach(shared_state);
        return 1;
    }

    Logger::log(LogSeverity::Info, Identity::Petent, "Petent pobiera bilet w rejestracji.");

    TicketIssuedMsg issued{};
    while (true) {
        int rc = ipc::msg::receive<TicketIssuedMsg>(msg_req_id, static_cast<long>(petent_id), &issued, 0);
        if (rc == -1) {
            if (errno == EINTR) {
                if (petent_evacuating) {
                    log_evacuation();
                    ipc::shm::detach(shared_state);
                    return 0;
                }
                continue;
            }
            std::string error = "Blad odbioru biletu: " + std::string(std::strerror(errno));
            Logger::log(LogSeverity::Err, Identity::Petent, error);
            ipc::shm::detach(shared_state);
            return 1;
        }
        break;
    }

    if (issued.reject_reason == TicketRejectReason::OfficeClosed) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Urzad zamkniety - bilet nie zostal wydany.");
        ipc::shm::detach(shared_state);
        return 0;
    }
    if (issued.reject_reason == TicketRejectReason::LimitReached) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Brak wolnych terminow - bilet nie zostal wydany.");
        ipc::shm::detach(shared_state);
        return 0;
    }
    if (issued.ticket_number == 0) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Bilet nie zostal wydany.");
        ipc::shm::detach(shared_state);
        return 0;
    }

    int dept_msg_id = ipc::helper::get_role_queue(issued.department);
    if (dept_msg_id == -1) {
        Logger::log(LogSeverity::Err, Identity::Petent, "Nie znaleziono kolejki urzednika.");
        ipc::shm::detach(shared_state);
        return 1;
    }

    if (ipc::msg::send<TicketIssuedMsg>(dept_msg_id, kUrzednikQueueType, issued) == -1) {
        Logger::log(LogSeverity::Err, Identity::Petent, "Blad wyslania biletu do urzednika.");
        ipc::shm::detach(shared_state);
        return 1;
    }

    Logger::log(LogSeverity::Info, Identity::Petent,
                "Petent zglosil sie do urzednika z biletem " + std::to_string(issued.ticket_number) + ".");

    ServiceDoneMsg done{};
    while (true) {
        int rc = ipc::msg::receive<ServiceDoneMsg>(msg_req_id, static_cast<long>(petent_id), &done, 0);
        if (rc == -1) {
            if (errno == EINTR) {
                if (petent_evacuating) {
                    log_evacuation();
                    ipc::shm::detach(shared_state);
                    return 0;
                }
                continue;
            }
            std::string error = "Blad odbioru potwierdzenia obslugi: " + std::string(std::strerror(errno));
            Logger::log(LogSeverity::Err, Identity::Petent, error);
            ipc::shm::detach(shared_state);
            return 1;
        }
        break;
    }

    ipc::shm::detach(shared_state);
    return 0;
}
