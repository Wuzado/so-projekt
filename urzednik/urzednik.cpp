#include "urzednik.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <sys/msg.h>
#include <thread>
#include <unistd.h>
#include "../ipcutils.h"
#include "../logger.h"
#include "../report.h"

constexpr long kUrzednikQueueType = 1;
static volatile sig_atomic_t urzednik_running = 1;
static volatile sig_atomic_t stop_after_current = 0;

static void handle_shutdown_signal(int) { urzednik_running = 0; }

static void handle_finish_signal(int) { stop_after_current = 1; }

static void short_work_delay(int time_mul) {
    int delay_minutes = rng::random_int(5, 30);
    if (time_mul <= 0) {
        time_mul = 1;
    }
    long long scaled_ms = static_cast<long long>(delay_minutes) * 60'000 / time_mul;
    if (scaled_ms <= 0) {
        scaled_ms = 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(scaled_ms));
}

static UrzednikRole get_rand_redirect() {
    static const UrzednikRole kTargets[] = {UrzednikRole::SC, UrzednikRole::KM, UrzednikRole::ML, UrzednikRole::PD};
    return kTargets[rng::random_int(0, 3)];
}

static uint32_t resolve_report_day(const SharedState* shared_state) {
    if (!shared_state) {
        return 1;
    }
    uint32_t day = shared_state->day;
    if (shared_state->office_status == OfficeStatus::Open) {
        day += 1;
    }
    if (day == 0) {
        day = 1;
    }
    return day;
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

    int msg_req_id = ipc::helper::get_msg_queue(ipc::KeyType::MsgQueueRejestracja);
    if (msg_req_id == -1) {
        Logger::log(LogSeverity::Err, Identity::Urzednik, role, "Nie znaleziono kolejki rejestracji.");
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

        short_work_delay(static_cast<int>(shared_state->time_mul));

        bool redirected = false;
        if (role == UrzednikRole::SA && shared_state->office_status == OfficeStatus::Open) {
            int roll = rng::random_int(1, 100);
            if (roll <= 40) {
                UrzednikRole target = get_rand_redirect();
                int target_msg_id = ipc::helper::get_role_queue(target);
                if (target_msg_id != -1) {
                    uint32_t ticket_number = 0;
                    bool limit_reached = false;
                    if (ipc::sem::wait(sem_id, 1) == -1) {
                        Logger::log(LogSeverity::Err, Identity::Urzednik, role,
                                    "Blad blokady licznika biletow dla przekierowania.");
                    }
                    else {
                        int target_idx = static_cast<int>(target);
                        uint32_t limit = shared_state->ticket_limits[target_idx];
                        uint32_t current = shared_state->ticket_counters[target_idx];
                        if (limit != 0 && current >= limit) {
                            limit_reached = true;
                        }
                        else {
                            ticket_number = ++shared_state->ticket_counters[target_idx];
                        }
                        if (ipc::sem::post(sem_id, 1) == -1) {
                            Logger::log(LogSeverity::Err, Identity::Urzednik, role,
                                        "Blad odblokowania licznika biletow dla przekierowania.");
                        }
                    }

                    if (limit_reached) {
                        auto dept_name = urzednik_role_to_string(target);
                        std::string dept = dept_name ? std::string(*dept_name) : std::string("?");
                        Logger::log(LogSeverity::Notice, Identity::Urzednik, role,
                                    "Brak wolnych terminow w wydziale " + dept +
                                        " dla petenta " + std::to_string(ticket.petent_id) +
                                        ", przekierowanie odrzucone.");
                        report::log_unserved_after_signal(resolve_report_day(shared_state), ticket.petent_id, target,
                                                         "SA");
                        redirected = true;
                    }
                    else {

                    TicketIssuedMsg redirect_msg{};
                    redirect_msg.petent_id = ticket.petent_id;
                    redirect_msg.ticket_number = ticket_number;
                    redirect_msg.department = target;
                    redirect_msg.redirected_from_sa = 1;
                    redirect_msg.reject_reason = TicketRejectReason::None;

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

            if (msg_req_id != -1) {
                ServiceDoneMsg done{};
                done.petent_id = ticket.petent_id;
                done.department = role;
                if (ipc::msg::send<ServiceDoneMsg>(msg_req_id, static_cast<long>(ticket.petent_id), done) == -1) {
                    Logger::log(LogSeverity::Err, Identity::Urzednik, role,
                                "Blad wyslania potwierdzenia obslugi petenta.");
                }
            }
        }

        if (stop_after_current) {
            break;
        }
    }

    if (stop_after_current) {
        bool logged_unserved = false;
        while (true) {
            TicketIssuedMsg ticket{};
            int rc = ipc::msg::receive<TicketIssuedMsg>(msg_id, kUrzednikQueueType, &ticket, IPC_NOWAIT);
            if (rc == -1) {
                if (errno == ENOMSG) {
                    break;
                }
                if (errno == EINTR) {
                    continue;
                }
                std::string error = "Blad odczytu kolejki podczas konczenia pracy: " +
                                    std::string(std::strerror(errno));
                Logger::log(LogSeverity::Err, Identity::Urzednik, role, error);
                break;
            }

            if (ticket.petent_id == 0) {
                continue;
            }

            std::string_view issuer = ticket.redirected_from_sa ? "SA" : "REJESTRACJA";
            report::log_unserved_after_signal(resolve_report_day(shared_state), ticket.petent_id, ticket.department,
                                              issuer);
            Logger::log(LogSeverity::Notice, Identity::Urzednik, role,
                        "skierowanie do " + std::string(urzednik_role_to_string(ticket.department).value_or("?")) +
                            " - wystawil " + std::string(issuer) +
                            " - petent " + std::to_string(ticket.petent_id));
            logged_unserved = true;
        }

        if (!logged_unserved) {
            report::log_unserved_after_signal(resolve_report_day(shared_state), 0, role, "DYREKTOR");
            Logger::log(LogSeverity::Notice, Identity::Urzednik, role,
                        "skierowanie do " + std::string(urzednik_role_to_string(role).value_or("?")) +
                            " - wystawil DYREKTOR - petent 0");
        }
    }

    ipc::shm::detach(shared_state);
    Logger::log(LogSeverity::Info, Identity::Urzednik, role, "Urzednik zakonczyl prace.");
    return 0;
}
