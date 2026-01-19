#include "generator.h"
#include <chrono>
#include <csignal>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include "../common.h"
#include "../ipcutils.h"
#include "../logger.h"

static volatile sig_atomic_t generator_running = 1;

static void handle_shutdown_signal(int) { generator_running = 0; }

static pid_t spawn_petent() {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        execl("/proc/self/exe", "so_projekt", "--role", "petent", static_cast<char*>(nullptr));
        perror("exec failed");
        _exit(1);
    }
    return pid;
}

static void reap_children() {
    int status = 0;
    while (waitpid(-1, &status, WNOHANG) > 0) {
    }
}

static void sleep_scaled_seconds(int seconds) {
    if (seconds <= 0) {
        return;
    }
    int base_ms = 1000 / TIME_MUL;
    if (base_ms <= 0) {
        base_ms = 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(base_ms * seconds));
}

int generator_main() {
    std::signal(SIGTERM, handle_shutdown_signal);
    std::signal(SIGINT, handle_shutdown_signal);

    Logger::log(LogSeverity::Info, Identity::Generator, "Generator petentow uruchomiony.");

    auto shared_state = ipc::helper::get_shared_state(true);
    if (!shared_state) {
        return 1;
    }

    while (generator_running) {
        if (shared_state->office_status == OfficeStatus::Open) {
            if (spawn_petent() == -1) {
                Logger::log(LogSeverity::Err, Identity::Generator, "Nie udalo sie utworzyc procesu petenta.");
            }
            int delay_sec = rng::random_int(1, 5);
            sleep_scaled_seconds(delay_sec);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        reap_children();
    }

    reap_children();
    ipc::shm::detach(shared_state);
    Logger::log(LogSeverity::Info, Identity::Generator, "Generator petentow zakonczyl prace.");
    return 0;
}
