#include "dyrektor.h"
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>
#include <vector>
#include "../common.h"
#include "../ipcutils.h"
#include "../logger.h"
#include "clock.h"

static void handle_shutdown_signal(int) {
    simulation_running = false;
}

void cleanup(SharedState* shared_state, int shm_id, int msg_req_id, int sem_id, int lock_file) {
    ipc::shm::detach(shared_state);
    ipc::shm::remove(shm_id);
    if (msg_req_id != -1) {
        ipc::msg::remove(msg_req_id);
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

static pid_t spawn_rejestracja() {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        execl("/proc/self/exe", "so_projekt", "--role", "rejestracja", static_cast<char*>(nullptr));
        perror("exec failed");
        _exit(1);
    }
    return pid;
}

static void terminate_rejestracja(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    waitpid(pid, nullptr, 0);
}

static void send_rejestracja_shutdown(int msg_req_id, int count) {
    if (msg_req_id == -1) {
        return;
    }
    TicketRequestMsg shutdown_msg{};
    shutdown_msg.petent_id = 0;
    for (int i = 0; i < count; ++i) {
        if (ipc::msg::send<TicketRequestMsg>(msg_req_id, 1, shutdown_msg) == -1) {
            break;
        }
    }
}

int dyrektor_main(HoursOpen hours_open) {
    std::signal(SIGINT, handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

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

    int shm_id = ipc::shm::create<SharedState>(shm_key);
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

    new (shared_state) SharedState(100);

    key_t msg_req_key = ipc::make_key(ipc::KeyType::MsgQueueRejestracja);
    if (msg_req_key == -1) {
        cleanup(shared_state, shm_id, -1, -1, lock_file);
        return 1;
    }

    int msg_req_id = ipc::msg::create(msg_req_key);
    if (msg_req_id == -1) {
        cleanup(shared_state, shm_id, -1, -1, lock_file);
        return 1;
    }

    key_t sem_key = ipc::make_key(ipc::KeyType::SemaphoreSet);
    if (sem_key == -1) {
        cleanup(shared_state, shm_id, msg_req_id, -1, lock_file);
        return 1;
    }

    int sem_id = ipc::sem::create(sem_key, 2);
    if (sem_id == -1) {
        cleanup(shared_state, shm_id, msg_req_id, -1, lock_file);
        return 1;
    }

    unsigned short sem_vals[2];
    unsigned int capacity = shared_state->building_capacity;
    unsigned short queue_slots = capacity > 1 ? (capacity - 1) : 1;
    sem_vals[0] = queue_slots;
    sem_vals[1] = 1;
    if (ipc::sem::set_all(sem_id, sem_vals) == -1) {
        cleanup(shared_state, shm_id, msg_req_id, sem_id, lock_file);
        return 1;
    }

    pthread_t clock_thread{};
    if (start_clock(shared_state, hours_open, &clock_thread) != 0) {
        cleanup(shared_state, shm_id, msg_req_id, sem_id, lock_file);
        return 1;
    }

    std::vector<pid_t> rejestracja_pids;
    rejestracja_pids.reserve(3);

    pid_t first_pid = spawn_rejestracja();
    if (first_pid == -1) {
        stop_clock(clock_thread);
        cleanup(shared_state, shm_id, msg_req_id, sem_id, lock_file);
        return 1;
    }
    rejestracja_pids.push_back(first_pid);
    shared_state->ticket_machines_num = 1;

    // Main dyrektor loop
    while (simulation_running.load()) {
        short target = desired_ticket_machines(shared_state);
        auto current = static_cast<short>(rejestracja_pids.size());

        while (current < target) {
            pid_t pid = spawn_rejestracja();
            if (pid == -1) {
                break;
            }
            rejestracja_pids.push_back(pid);
            current = static_cast<short>(rejestracja_pids.size());
        }

        while (current > target) {
            pid_t pid = rejestracja_pids.back();
            rejestracja_pids.pop_back();
            send_rejestracja_shutdown(msg_req_id, 1);
            terminate_rejestracja(pid);
            current = static_cast<short>(rejestracja_pids.size());
        }

        shared_state->ticket_machines_num = current;

        int status = 0;
        while (waitpid(-1, &status, WNOHANG) > 0) {
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    stop_clock(clock_thread);

    send_rejestracja_shutdown(msg_req_id, static_cast<int>(rejestracja_pids.size()));

    for (pid_t pid : rejestracja_pids) {
        terminate_rejestracja(pid);
    }

    cleanup(shared_state, shm_id, msg_req_id, sem_id, lock_file);
    return 0;
}
