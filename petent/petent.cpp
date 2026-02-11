#include "petent.h"
#include "dziecko.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <string>
#include <unistd.h>
#include "../ipcutils.h"
#include "../logger.h"

static volatile sig_atomic_t petent_evacuating = 0;

static void handle_evacuation_signal(int) { petent_evacuating = 1; }

static void log_evacuation() {
    Logger::log(LogSeverity::Notice, Identity::Petent, "Ewakuacja - petent opuszcza budynek.");
}

int petent_main(UrzednikRole department, bool is_vip, bool has_child) {
    ipc::install_signal_handler(SIGUSR2, handle_evacuation_signal);
    ipc::install_signal_handler(SIGTERM, handle_evacuation_signal);
    ipc::install_signal_handler(SIGINT, handle_evacuation_signal);

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

    // +1 capacity if has a child
    if (has_child) {
        if (ipc::sem::wait(sem_id, 0) == -1) {
            if (errno == EINTR && petent_evacuating) {
                log_evacuation();
                ipc::sem::post(sem_id, 0); // release parent's slot
                ipc::shm::detach(shared_state);
                return 0;
            }
            Logger::log(LogSeverity::Err, Identity::Petent, "Blad oczekiwania na miejsce dla dziecka.");
            ipc::sem::post(sem_id, 0); // release parent's slot
            ipc::shm::detach(shared_state);
            return 1;
        }
    }

    // Child thread state
    bool child_spawned = false;
    ChildThreadData child_data{};
    pthread_t child_thread{};

    // Cleanup helper: signal child done, join, release extra capacity slot
    auto cleanup_child = [&]() {
        if (child_spawned) {
            child_signal_done(&child_data);
            child_join_and_cleanup(&child_data, child_thread);
            ipc::sem::post(sem_id, 0); // release child's capacity slot
        }
    };

    if (has_child) {
        if (child_init(&child_data, getpid(), &petent_evacuating) == -1) {
            Logger::log(LogSeverity::Err, Identity::Petent, "Blad inicjalizacji watku dziecka.");
            ipc::sem::post(sem_id, 0); // release child's slot
            ipc::sem::post(sem_id, 0); // release parent's slot
            ipc::shm::detach(shared_state);
            return 1;
        }
        if (child_start(&child_data, &child_thread) == -1) {
            Logger::log(LogSeverity::Err, Identity::Petent, "Blad uruchomienia watku dziecka.");
            ipc::mutex::destroy(&child_data.mutex);
            ipc::cond::destroy(&child_data.cond);
            ipc::sem::post(sem_id, 0); // release child's slot
            ipc::sem::post(sem_id, 0); // release parent's slot
            ipc::shm::detach(shared_state);
            return 1;
        }
        child_spawned = true;
        Logger::log(LogSeverity::Info, Identity::Petent, "Petent wchodzi do urzedu z dzieckiem.");
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
    request.is_vip = is_vip;
    request.has_child = has_child ? 1 : 0;

    if (is_vip) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Petent VIP - wysylam zadanie biletu.");
    }

    if (ipc::msg::send<TicketRequestMsg>(msg_req_id, kTicketRequestType, request) == -1) {
        Logger::log(LogSeverity::Err, Identity::Petent, "Blad wyslania prosby o bilet.");
        if (ipc::sem::wait(sem_id, 1) == 0) {
            if (shared_state->current_queue_length > 0) {
                shared_state->current_queue_length--;
            }
            ipc::sem::post(sem_id, 1);
        }
        ipc::sem::post(sem_id, 0);
        cleanup_child();
        ipc::shm::detach(shared_state);
        return 1;
    }

    Logger::log(LogSeverity::Info, Identity::Petent, "Petent pobiera bilet w rejestracji.");

    TicketIssuedMsg issued{};
    while (true) {
        if (petent_evacuating) {
            log_evacuation();
            cleanup_child();
            ipc::shm::detach(shared_state);
            return 0;
        }
        int rc = ipc::msg::receive<TicketIssuedMsg>(msg_req_id, static_cast<long>(petent_id), &issued, 0);
        if (rc == -1) {
            if (errno == EINTR) {
                if (petent_evacuating) {
                    log_evacuation();
                    cleanup_child();
                    ipc::shm::detach(shared_state);
                    return 0;
                }
                continue;
            }
            std::string error = "Blad odbioru biletu: " + std::string(std::strerror(errno));
            Logger::log(LogSeverity::Err, Identity::Petent, error);
            cleanup_child();
            ipc::shm::detach(shared_state);
            return 1;
        }
        break;
    }

    if (issued.reject_reason == TicketRejectReason::OfficeClosed) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Urzad zamkniety - bilet nie zostal wydany.");
        // Building slot already freed by rejestracja (parent only); free child slot
        cleanup_child();
        ipc::shm::detach(shared_state);
        return 0;
    }
    if (issued.reject_reason == TicketRejectReason::LimitReached) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Brak wolnych terminow - bilet nie zostal wydany.");
        // Building slot already freed by rejestracja (parent only); free child slot
        cleanup_child();
        ipc::shm::detach(shared_state);
        return 0;
    }
    if (issued.ticket_number == 0) {
        Logger::log(LogSeverity::Notice, Identity::Petent, "Bilet nie zostal wydany.");
        cleanup_child();
        ipc::shm::detach(shared_state);
        return 0;
    }

    int dept_msg_id = ipc::helper::get_role_queue(issued.department);
    if (dept_msg_id == -1) {
        Logger::log(LogSeverity::Err, Identity::Petent, "Nie znaleziono kolejki urzednika.");
        cleanup_child();
        ipc::shm::detach(shared_state);
        return 1;
    }

    long queue_mtype = issued.is_vip ? kVipQueueType : kNormalQueueType;
    if (ipc::msg::send<TicketIssuedMsg>(dept_msg_id, queue_mtype, issued) == -1) {
        Logger::log(LogSeverity::Err, Identity::Petent, "Blad wyslania biletu do urzednika.");
        cleanup_child();
        ipc::shm::detach(shared_state);
        return 1;
    }

    if (issued.is_vip) {
        Logger::log(LogSeverity::Info, Identity::Petent,
                    "Petent VIP - wchodzi do kolejki priorytetowej z biletem " + std::to_string(issued.ticket_number) + ".");
    } else {
        Logger::log(LogSeverity::Info, Identity::Petent,
                    "Petent zglosil sie do urzednika z biletem " + std::to_string(issued.ticket_number) + ".");
    }

    ServiceDoneMsg done{};
    while (true) {
        if (petent_evacuating) {
            log_evacuation();
            cleanup_child();
            ipc::shm::detach(shared_state);
            return 0;
        }
        int rc = ipc::msg::receive<ServiceDoneMsg>(msg_req_id, static_cast<long>(petent_id), &done, 0);
        if (rc == -1) {
            if (errno == EINTR) {
                if (petent_evacuating) {
                    log_evacuation();
                    cleanup_child();
                    ipc::shm::detach(shared_state);
                    return 0;
                }
                continue;
            }
            std::string error = "Blad odbioru potwierdzenia obslugi: " + std::string(std::strerror(errno));
            Logger::log(LogSeverity::Err, Identity::Petent, error);
            cleanup_child();
            ipc::shm::detach(shared_state);
            return 1;
        }

        if (done.action == ServiceAction::GoToKasa) {
            Logger::log(LogSeverity::Info, Identity::Petent,
                        "Petent skierowany do kasy - udaje sie dokonac oplaty.");

            int msg_kasa_id = ipc::helper::get_msg_queue(ipc::KeyType::MsgQueueKasa);
            if (msg_kasa_id == -1) {
                Logger::log(LogSeverity::Err, Identity::Petent, "Nie znaleziono kolejki kasy.");
                cleanup_child();
                ipc::shm::detach(shared_state);
                return 1;
            }

            KasaRequestMsg pay{};
            pay.petent_id = petent_id;
            pay.department = done.department;
            if (ipc::msg::send<KasaRequestMsg>(msg_kasa_id, kKasaRequestType, pay) == -1) {
                Logger::log(LogSeverity::Err, Identity::Petent, "Blad wyslania zadania oplaty do kasy.");
                cleanup_child();
                ipc::shm::detach(shared_state);
                return 1;
            }

            ServiceDoneMsg kasa_done{};
            bool paid = false;
            while (!paid) {
                if (petent_evacuating) {
                    log_evacuation();
                    cleanup_child();
                    ipc::shm::detach(shared_state);
                    return 0;
                }
                int crc = ipc::msg::receive<ServiceDoneMsg>(msg_req_id, static_cast<long>(petent_id), &kasa_done, 0);
                if (crc == -1) {
                    if (errno == EINTR) {
                        if (petent_evacuating) {
                            log_evacuation();
                            cleanup_child();
                            ipc::shm::detach(shared_state);
                            return 0;
                        }
                        continue;
                    }
                    Logger::log(LogSeverity::Err, Identity::Petent, "Blad odbioru potwierdzenia oplaty.");
                    cleanup_child();
                    ipc::shm::detach(shared_state);
                    return 1;
                }
                paid = true;
            }

            Logger::log(LogSeverity::Info, Identity::Petent,
                        "Petent dokonal oplaty - wraca do urzednika.");

            KasaRequestMsg ret{};
            ret.petent_id = petent_id;
            ret.department = done.department;
            if (ipc::msg::send<KasaRequestMsg>(dept_msg_id, kKasaReturnQueueType, ret) == -1) {
                Logger::log(LogSeverity::Err, Identity::Petent, "Blad powiadomienia urzednika o powrocie z kasy.");
                cleanup_child();
                ipc::shm::detach(shared_state);
                return 1;
            }

            continue;
        }

        break;
    }

    cleanup_child();
    ipc::shm::detach(shared_state);
    return 0;
}
