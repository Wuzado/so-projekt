#include "dyrektor.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>
#include "../common.h"
#include "../ipcutils.h"
#include "../logger.h"
#include "../report.h"
#include "clock.h"
#include "process.h"

static void handle_shutdown_signal(int) { simulation_running = false; }

void cleanup(SharedState* shared_state, int shm_id, int msg_req_id, int msg_sa_id, int msg_sc_id, int msg_km_id,
             int msg_ml_id, int msg_pd_id, int sem_id, int lock_file) {
    ipc::shm::detach(shared_state);
    ipc::shm::remove(shm_id);
    if (msg_req_id != -1) {
        ipc::msg::remove(msg_req_id);
    }
    if (msg_sa_id != -1) {
        ipc::msg::remove(msg_sa_id);
    }
    if (msg_sc_id != -1) {
        ipc::msg::remove(msg_sc_id);
    }
    if (msg_km_id != -1) {
        ipc::msg::remove(msg_km_id);
    }
    if (msg_ml_id != -1) {
        ipc::msg::remove(msg_ml_id);
    }
    if (msg_pd_id != -1) {
        ipc::msg::remove(msg_pd_id);
    }
    if (sem_id != -1) {
        ipc::sem::remove(sem_id);
    }
    close(lock_file);
    unlink(ipc::IPC_LOCK_FILE);
}

static short desired_ticket_machines(const SharedState* shared_state) {
    if (!shared_state) {
        return 1;
    }

    unsigned int capacity = shared_state->building_capacity;
    unsigned int k = capacity / 3u;
    if (k == 0) {
        k = 1;
    }

    unsigned int queue_len = shared_state->current_queue_length;
    if (queue_len > 2 * k) {
        return 3;
    }
    if (queue_len > k) {
        return 2;
    }
    return 1;
}

static void drain_unserved_tickets(const std::vector<process::UrzednikQueue>& queues, uint32_t report_day) {
    for (const auto& queue : queues) {
        while (true) {
            TicketIssuedMsg ticket{};
            int rc = ipc::msg::receive<TicketIssuedMsg>(queue.msg_id, 1, &ticket, IPC_NOWAIT);
            if (rc == -1) {
                if (errno == ENOMSG) {
                    break;
                }
                break;
            }

            if (ticket.petent_id == 0) {
                continue;
            }

            report::log_unserved_after_close(report_day, ticket.petent_id, ticket.department, ticket.ticket_number);
        }
    }
}

using process::UrzednikProcess;
using process::UrzednikQueue;

int dyrektor_main(HoursOpen hours_open, const std::array<uint32_t, 5>& department_limits, int time_mul,
                  int gen_min_delay_sec, int gen_max_delay_sec, bool spawn_generator) {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);
    std::signal(SIGUSR2, handle_shutdown_signal);

    process::group::init_self();

    int lock_file = open(ipc::IPC_LOCK_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (lock_file == -1) {
        std::string error = "Nie udalo sie utworzyc pliku blokady IPC: " + std::string(std::strerror(errno));
        Logger::log(LogSeverity::Emerg, Identity::Dyrektor, error);
        return 1;
    }

    Logger::log(LogSeverity::Info, Identity::Dyrektor, "Dyrektor uruchomiony pomyslnie.");

    key_t shm_key = ipc::make_key(ipc::KeyType::SharedState);
    if (shm_key == -1) {
        close(lock_file);
        return 1;
    }

    int shm_id = ipc::helper::create_or_reset_shm(shm_key);
    if (shm_id == -1) {
        close(lock_file);
        return 1;
    }

    auto shared_state = ipc::shm::attach<SharedState>(shm_id, false);
    if (!shared_state) {
        close(lock_file);
        ipc::shm::remove(shm_id);
        return 1;
    }

    std::array<uint32_t, 5> ticket_limits = {
        department_limits[1],
        department_limits[2],
        department_limits[3],
        department_limits[4],
        department_limits[0] * 2u
    };

    new (shared_state) SharedState(100, ticket_limits, static_cast<uint32_t>(time_mul));

    key_t msg_req_key = ipc::make_key(ipc::KeyType::MsgQueueRejestracja);
    if (msg_req_key == -1) {
        cleanup(shared_state, shm_id, -1, -1, -1, -1, -1, -1, -1, lock_file);
        return 1;
    }

    int msg_req_id = ipc::helper::create_or_reset_msg(msg_req_key);
    if (msg_req_id == -1) {
        cleanup(shared_state, shm_id, -1, -1, -1, -1, -1, -1, -1, lock_file);
        return 1;
    }

    key_t msg_sa_key = ipc::make_key(ipc::KeyType::MsgQueueSA);
    key_t msg_sc_key = ipc::make_key(ipc::KeyType::MsgQueueSC);
    key_t msg_km_key = ipc::make_key(ipc::KeyType::MsgQueueKM);
    key_t msg_ml_key = ipc::make_key(ipc::KeyType::MsgQueueML);
    key_t msg_pd_key = ipc::make_key(ipc::KeyType::MsgQueuePD);
    if (msg_sa_key == -1 || msg_sc_key == -1 || msg_km_key == -1 || msg_ml_key == -1 || msg_pd_key == -1) {
        cleanup(shared_state, shm_id, msg_req_id, -1, -1, -1, -1, -1, -1, lock_file);
        return 1;
    }

    int msg_sa_id = ipc::helper::create_or_reset_msg(msg_sa_key);
    int msg_sc_id = ipc::helper::create_or_reset_msg(msg_sc_key);
    int msg_km_id = ipc::helper::create_or_reset_msg(msg_km_key);
    int msg_ml_id = ipc::helper::create_or_reset_msg(msg_ml_key);
    int msg_pd_id = ipc::helper::create_or_reset_msg(msg_pd_key);
    if (msg_sa_id == -1 || msg_sc_id == -1 || msg_km_id == -1 || msg_ml_id == -1 || msg_pd_id == -1) {
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, -1, lock_file);
        return 1;
    }

    const std::vector<UrzednikQueue> urzednik_queues = {
        {msg_sa_id, UrzednikRole::SA, 2}, {msg_sc_id, UrzednikRole::SC, 1}, {msg_km_id, UrzednikRole::KM, 1},
        {msg_ml_id, UrzednikRole::ML, 1}, {msg_pd_id, UrzednikRole::PD, 1},
    };

    key_t sem_key = ipc::make_key(ipc::KeyType::SemaphoreSet);
    if (sem_key == -1) {
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, -1, lock_file);
        return 1;
    }

    int sem_id = ipc::helper::create_or_reset_sem(sem_key, 2);
    if (sem_id == -1) {
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, -1, lock_file);
        return 1;
    }

    unsigned short sem_vals[2];
    unsigned int capacity = shared_state->building_capacity;
    unsigned short queue_slots = capacity > 1 ? (capacity - 1) : 1;
    sem_vals[0] = queue_slots;
    sem_vals[1] = 1;
    if (ipc::sem::set_all(sem_id, sem_vals) == -1) {
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id,
                lock_file);
        return 1;
    }

    process::ProcessConfig process_config{hours_open, department_limits, time_mul, gen_min_delay_sec, gen_max_delay_sec};

    std::vector<UrzednikProcess> urzednik_pids;
    pid_t generator_pid = -1;
    if (spawn_generator) {
        generator_pid = process::spawn_generator(process_config);
        if (generator_pid == -1) {
            cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id,
                    lock_file);
            return 1;
        }
    }
    if (!process::spawn_urzednicy(urzednik_pids, process_config)) {
        if (generator_pid != -1) {
            process::terminate_generator(generator_pid);
        }
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id,
                lock_file);
        return 1;
    }
    std::vector<pid_t> rejestracja_pids;
    if (!process::spawn_rejestracja_group(rejestracja_pids, process_config)) {
        process::send_urzednik_shutdowns(urzednik_queues);
        process::terminate_urzednik_all(urzednik_pids);
        if (generator_pid != -1) {
            process::terminate_generator(generator_pid);
        }
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id,
                lock_file);
        return 1;
    }
    shared_state->ticket_machines_num = 1;

    pthread_t clock_thread{};
    if (start_clock(shared_state, hours_open, &clock_thread) != 0) {
        process::send_rejestracja_shutdown(msg_req_id, static_cast<int>(rejestracja_pids.size()));
        process::send_urzednik_shutdowns(urzednik_queues);
        process::terminate_rejestracja_all(rejestracja_pids);
        process::terminate_urzednik_all(urzednik_pids);
        if (generator_pid != -1) {
            process::terminate_generator(generator_pid);
        }
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id,
                lock_file);
        return 1;
    }

    uint32_t last_day = shared_state->day;

    // Main dyrektor loop
    while (simulation_running.load()) {
        if (shared_state->day != last_day) {
            uint32_t report_day = last_day + 1;
            Logger::log(LogSeverity::Notice, Identity::Dyrektor, "Restart dzienny urzednikow i rejestracji.");

            process::stop_daily_rejestracja(rejestracja_pids);
            process::stop_daily_urzednik(urzednik_pids);

            drain_unserved_tickets(urzednik_queues, report_day);

            shared_state->current_queue_length = 0;
            for (auto& counter : shared_state->ticket_counters) {
                counter = 0;
            }

            if (!process::spawn_urzednicy(urzednik_pids, process_config)) {
                Logger::log(LogSeverity::Emerg, Identity::Dyrektor, "Nie udalo sie odtworzyc urzednikow po dniu.");
                simulation_running = false;
                break;
            }
            if (!process::spawn_rejestracja_group(rejestracja_pids, process_config)) {
                Logger::log(LogSeverity::Emerg, Identity::Dyrektor, "Nie udalo sie odtworzyc rejestracji po dniu.");
                process::terminate_urzednik_all(urzednik_pids);
                simulation_running = false;
                break;
            }

            shared_state->ticket_machines_num = static_cast<uint8_t>(rejestracja_pids.size());
            notify_day_restart_complete();
            last_day = shared_state->day;
        }

        short target = desired_ticket_machines(shared_state);
        auto current = static_cast<short>(rejestracja_pids.size());

        while (current < target) {
            pid_t pid = process::spawn_rejestracja(process_config);
            if (pid == -1) {
                break;
            }
            rejestracja_pids.push_back(pid);
            current = static_cast<short>(rejestracja_pids.size());
        }

        while (current > target) {
            pid_t pid = rejestracja_pids.back();
            rejestracja_pids.pop_back();
            process::send_rejestracja_shutdown(msg_req_id, 1);
            process::terminate_rejestracja(pid);
            current = static_cast<short>(rejestracja_pids.size());
        }

        shared_state->ticket_machines_num = current;

        int status = 0;
        while (waitpid(-1, &status, WNOHANG) > 0) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    process::group::signal_self(SIGUSR2);
    stop_clock(clock_thread);

    process::send_rejestracja_shutdown(msg_req_id, static_cast<int>(rejestracja_pids.size()));
    process::send_urzednik_shutdowns(urzednik_queues);

    process::terminate_rejestracja_all(rejestracja_pids);
    process::terminate_urzednik_all(urzednik_pids);
    if (generator_pid != -1) {
        process::terminate_generator(generator_pid);
    }

    cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id, lock_file);
    return 0;
}
