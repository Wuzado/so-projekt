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

static pid_t spawn_urzednik(UrzednikRole role) {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        const char* dept = "SA";
        switch (role) {
            case UrzednikRole::SA:
                dept = "SA";
                break;
            case UrzednikRole::SC:
                dept = "SC";
                break;
            case UrzednikRole::KM:
                dept = "KM";
                break;
            case UrzednikRole::ML:
                dept = "ML";
                break;
            case UrzednikRole::PD:
                dept = "PD";
                break;
        }
        execl("/proc/self/exe", "so_projekt", "--role", "urzednik", "--dept", dept,
              static_cast<char*>(nullptr));
        perror("exec failed");
        _exit(1);
    }
    return pid;
}

static void terminate_urzednik(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    waitpid(pid, nullptr, 0);
}

struct UrzednikProcess {
    pid_t pid;
    UrzednikRole role;
};

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

static void send_urzednik_shutdown(int msg_id, UrzednikRole role, int count) {
    if (msg_id == -1) {
        return;
    }
    TicketIssuedMsg shutdown_msg{};
    shutdown_msg.petent_id = 0;
    shutdown_msg.ticket_number = 0;
    shutdown_msg.department = role;
    shutdown_msg.redirected_from_sa = 0;
    for (int i = 0; i < count; ++i) {
        if (ipc::msg::send<TicketIssuedMsg>(msg_id, 1, shutdown_msg) == -1) {
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
        cleanup(shared_state, shm_id, -1, -1, -1, -1, -1, -1, -1, lock_file);
        return 1;
    }

    int msg_req_id = ipc::msg::create(msg_req_key);
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

    int msg_sa_id = ipc::msg::create(msg_sa_key);
    int msg_sc_id = ipc::msg::create(msg_sc_key);
    int msg_km_id = ipc::msg::create(msg_km_key);
    int msg_ml_id = ipc::msg::create(msg_ml_key);
    int msg_pd_id = ipc::msg::create(msg_pd_key);
    if (msg_sa_id == -1 || msg_sc_id == -1 || msg_km_id == -1 || msg_ml_id == -1 || msg_pd_id == -1) {
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, -1, lock_file);
        return 1;
    }

    key_t sem_key = ipc::make_key(ipc::KeyType::SemaphoreSet);
    if (sem_key == -1) {
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, -1, lock_file);
        return 1;
    }

    int sem_id = ipc::sem::create(sem_key, 2);
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
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id, lock_file);
        return 1;
    }

    pthread_t clock_thread{};
    if (start_clock(shared_state, hours_open, &clock_thread) != 0) {
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id, lock_file);
        return 1;
    }

    std::vector<UrzednikProcess> urzednik_pids;
    urzednik_pids.reserve(6);
    pid_t sa1 = spawn_urzednik(UrzednikRole::SA);
    pid_t sa2 = spawn_urzednik(UrzednikRole::SA);
    pid_t sc = spawn_urzednik(UrzednikRole::SC);
    pid_t km = spawn_urzednik(UrzednikRole::KM);
    pid_t ml = spawn_urzednik(UrzednikRole::ML);
    pid_t pd = spawn_urzednik(UrzednikRole::PD);
    if (sa1 == -1 || sa2 == -1 || sc == -1 || km == -1 || ml == -1 || pd == -1) {
        stop_clock(clock_thread);
        int sa_count = (sa1 > 0 ? 1 : 0) + (sa2 > 0 ? 1 : 0);
        int sc_count = sc > 0 ? 1 : 0;
        int km_count = km > 0 ? 1 : 0;
        int ml_count = ml > 0 ? 1 : 0;
        int pd_count = pd > 0 ? 1 : 0;
        send_urzednik_shutdown(msg_sa_id, UrzednikRole::SA, sa_count);
        send_urzednik_shutdown(msg_sc_id, UrzednikRole::SC, sc_count);
        send_urzednik_shutdown(msg_km_id, UrzednikRole::KM, km_count);
        send_urzednik_shutdown(msg_ml_id, UrzednikRole::ML, ml_count);
        send_urzednik_shutdown(msg_pd_id, UrzednikRole::PD, pd_count);
        if (sa1 > 0) waitpid(sa1, nullptr, 0);
        if (sa2 > 0) waitpid(sa2, nullptr, 0);
        if (sc > 0) waitpid(sc, nullptr, 0);
        if (km > 0) waitpid(km, nullptr, 0);
        if (ml > 0) waitpid(ml, nullptr, 0);
        if (pd > 0) waitpid(pd, nullptr, 0);
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id, lock_file);
        return 1;
    }
    urzednik_pids.push_back({sa1, UrzednikRole::SA});
    urzednik_pids.push_back({sa2, UrzednikRole::SA});
    urzednik_pids.push_back({sc, UrzednikRole::SC});
    urzednik_pids.push_back({km, UrzednikRole::KM});
    urzednik_pids.push_back({ml, UrzednikRole::ML});
    urzednik_pids.push_back({pd, UrzednikRole::PD});

    std::vector<pid_t> rejestracja_pids;
    rejestracja_pids.reserve(3);

    pid_t first_pid = spawn_rejestracja();
    if (first_pid == -1) {
        stop_clock(clock_thread);
        send_urzednik_shutdown(msg_sa_id, UrzednikRole::SA, 2);
        send_urzednik_shutdown(msg_sc_id, UrzednikRole::SC, 1);
        send_urzednik_shutdown(msg_km_id, UrzednikRole::KM, 1);
        send_urzednik_shutdown(msg_ml_id, UrzednikRole::ML, 1);
        send_urzednik_shutdown(msg_pd_id, UrzednikRole::PD, 1);
        for (const auto& proc : urzednik_pids) {
            terminate_urzednik(proc.pid);
        }
        cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id, lock_file);
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
    send_urzednik_shutdown(msg_sa_id, UrzednikRole::SA, 2);
    send_urzednik_shutdown(msg_sc_id, UrzednikRole::SC, 1);
    send_urzednik_shutdown(msg_km_id, UrzednikRole::KM, 1);
    send_urzednik_shutdown(msg_ml_id, UrzednikRole::ML, 1);
    send_urzednik_shutdown(msg_pd_id, UrzednikRole::PD, 1);

    for (pid_t pid : rejestracja_pids) {
        terminate_rejestracja(pid);
    }

    for (const auto& proc : urzednik_pids) {
        terminate_urzednik(proc.pid);
    }

    cleanup(shared_state, shm_id, msg_req_id, msg_sa_id, msg_sc_id, msg_km_id, msg_ml_id, msg_pd_id, sem_id, lock_file);
    return 0;
}
