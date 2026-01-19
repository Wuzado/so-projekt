#include "urzednik.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include "../ipcutils.h"
#include "../logger.h"

constexpr long kUrzednikQueueType = 1;
static volatile sig_atomic_t urzednik_running = 1;
static volatile sig_atomic_t stop_after_current = 0;

static void handle_shutdown_signal(int) { urzednik_running = 0; }

static void handle_finish_signal(int) { stop_after_current = 1; }

static void short_work_delay() {
    int delay_ms = rng::random_int(5, 30);
    std::this_thread::sleep_for(std::chrono::minutes(delay_ms / TIME_MUL));
}

static UrzednikRole get_rand_redirect() {
    static const UrzednikRole kTargets[] = {UrzednikRole::SC, UrzednikRole::KM, UrzednikRole::ML, UrzednikRole::PD};
    return kTargets[rng::random_int(0, 3)];
}

int urzednik_main(UrzednikRole role) {
    std::signal(SIGTERM, handle_shutdown_signal);
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGUSR1, handle_finish_signal);

    Logger::log(LogSeverity::Info, Identity::Urzednik, role, "Urzednik uruchomiony.");

    auto shared_state = ipc::helper::get_shared_state(false);
    if (!shared_state) {
        return 1;
    }

    int sem_id = ipc::helper::get_semaphore_set(2);
    if (sem_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    int msg_id = ipc::helper::get_role_queue(role);
    if (msg_id == -1) {
        ipc::shm::detach(shared_state);
        return 1;
    }

    while (urzednik_running) {
        TicketIssuedMsg ticket{};
        int rc = ipc::msg::receive<TicketIssuedMsg>(msg_id, kUrzednikQueueType, &ticket, 0);
        if (rc == -1) {
            if (errno == EINTR) {
                if (!urzednik_running || stop_after_current) {
                    break;
                }
                continue;
            }
            std::string error = "Blad odbioru petenta: " + std::string(std::strerror(errno));
            Logger::log(LogSeverity::Err, Identity::Urzednik, role, error);
            continue;
        }

        if (ticket.petent_id == 0) {
            Logger::log(LogSeverity::Notice, Identity::Urzednik, role, "Otrzymano sygnal zakonczenia.");
            break;
        }

        Logger::log(LogSeverity::Info, Identity::Urzednik, role,
                    "Rozpoczecie obslugi petenta " + std::to_string(ticket.petent_id) + " (bilet " +
                        std::to_string(ticket.ticket_number) + ").");

        short_work_delay();

        bool redirected = false;
        if (role == UrzednikRole::SA && shared_state->office_status == OfficeStatus::Open) {
            int roll = rng::random_int(1, 100);
            if (roll <= 40) {
                UrzednikRole target = get_rand_redirect();
                int target_msg_id = ipc::helper::get_role_queue(target);
                if (target_msg_id != -1) {
                    uint32_t ticket_number = 0;
                    if (ipc::sem::wait(sem_id, 1) == -1) {
                        Logger::log(LogSeverity::Err, Identity::Urzednik, role,
                                    "Blad blokady licznika biletow dla przekierowania.");
                    }
                    else {
                        ticket_number = ++shared_state->ticket_counters[static_cast<int>(target)];
                        if (ipc::sem::post(sem_id, 1) == -1) {
                            Logger::log(LogSeverity::Err, Identity::Urzednik, role,
                                        "Blad odblokowania licznika biletow dla przekierowania.");
                        }
                    }

                    TicketIssuedMsg redirect_msg{};
                    redirect_msg.petent_id = ticket.petent_id;
                    redirect_msg.ticket_number = ticket_number;
                    redirect_msg.department = target;
                    redirect_msg.redirected_from_sa = 1;

                    if (ipc::msg::send<TicketIssuedMsg>(target_msg_id, kUrzednikQueueType, redirect_msg) == -1) {
                        std::string error =
                            "Blad wyslania przekierowania dla petenta " + std::to_string(ticket.petent_id);
                        Logger::log(LogSeverity::Err, Identity::Urzednik, role, error);
                    }
                    else {
                        Logger::log(LogSeverity::Notice, Identity::Urzednik, role,
                                    "Przekierowano petenta " + std::to_string(ticket.petent_id) +
                                        " do innego wydzialu.");
                        redirected = true;
                    }
                }
                else {
                    Logger::log(LogSeverity::Err, Identity::Urzednik, role,
                                "Brak kolejki docelowej do przekierowania.");
                }
            }
        }

        if (!redirected) {
            Logger::log(LogSeverity::Info, Identity::Urzednik, role,
                        "Zakonczono obsluge petenta " + std::to_string(ticket.petent_id) + ".");
        }

        if (stop_after_current) {
            break;
        }
    }

    ipc::shm::detach(shared_state);
    Logger::log(LogSeverity::Info, Identity::Urzednik, role, "Urzednik zakonczyl prace.");
    return 0;
}
