#include "dyrektor.h"
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <pthread.h>
#include <thread>
#include <unistd.h>
#include "../common.h"
#include "../ipcutils.h"
#include "../logger.h"
#include "clock.h"

static void handle_shutdown_signal(int) {
    simulation_running = false;
}

void cleanup(SharedState* shared_state, int shm_id, int lock_file) {
    ipc::shm::detach(shared_state);
    ipc::shm::remove(shm_id);
    close(lock_file);
    unlink(ipc::IPC_LOCK_FILE);
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

    pthread_t clock_thread{};
    if (start_clock(shared_state, hours_open, &clock_thread) != 0) {
        cleanup(shared_state, shm_id, lock_file);
        return 1;
    }

    // Main dyrektor loop
    while (simulation_running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    stop_clock(clock_thread);

    cleanup(shared_state, shm_id, lock_file);
    return 0;
}
