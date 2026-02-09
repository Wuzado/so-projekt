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

static UrzednikRole choose_department() {
    int roll = rng::random_int(1, 100);
    if (roll <= 60) {
        return UrzednikRole::SA;
    }
    if (roll <= 70) {
        return UrzednikRole::SC;
    }
    if (roll <= 80) {
        return UrzednikRole::KM;
    }
    if (roll <= 90) {
        return UrzednikRole::ML;
    }
    return UrzednikRole::PD;
}

static pid_t spawn_petent() {
    pid_t pid = fork();
    if (pid == -1) {
        perror("fork failed");
        return -1;
    }
    if (pid == 0) {
        UrzednikRole department = choose_department();
        auto dept_name = urzednik_role_to_string(department);
        if (!dept_name) {
            Logger::log(LogSeverity::Err, Identity::Generator, "Nieznany wydzial petenta.");
            _exit(1);
        }
        execl("/proc/self/exe", "so_projekt", "--role", "petent", "--dept", dept_name->data(),
              static_cast<char*>(nullptr));
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

static void sleep_scaled_seconds(int seconds, int time_mul) {
    if (seconds <= 0) {
        return;
    }
    if (time_mul <= 0) {
        time_mul = 1;
    }
    int base_ms = 1000 / time_mul;
    if (base_ms <= 0) {
        base_ms = 1;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(base_ms * seconds));
}

int generator_main(int min_delay_sec, int max_delay_sec, int time_mul, int max_count) {
    ipc::install_signal_handler(SIGTERM, handle_shutdown_signal);
    signal(SIGINT, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    Logger::log(LogSeverity::Info, Identity::Generator, "Generator petentow uruchomiony.");

    auto shared_state = ipc::helper::get_shared_state(true);
    if (!shared_state) {
        return 1;
    }

    if (min_delay_sec < 0) {
        min_delay_sec = 0;
    }
    if (max_delay_sec < min_delay_sec) {
        max_delay_sec = min_delay_sec;
    }

    int generated_count = 0;
    if (max_count == 0) {
        Logger::log(LogSeverity::Notice, Identity::Generator, "Limit petentow ustawiony na 0 - generator konczy prace.");
        ipc::shm::detach(shared_state);
        return 0;
    }

    while (generator_running) {
        if (max_count > 0 && generated_count >= max_count) {
            Logger::log(LogSeverity::Notice, Identity::Generator,
                        "Osiagnieto limit generowania petentow: " + std::to_string(max_count) + ".");
            break;
        }
        if (shared_state->office_status == OfficeStatus::Open) {
            if (spawn_petent() == -1) {
                Logger::log(LogSeverity::Err, Identity::Generator, "Nie udalo sie utworzyc procesu petenta.");
            } else {
                generated_count++;
            }
            int delay_sec = rng::random_int(min_delay_sec, max_delay_sec);
            sleep_scaled_seconds(delay_sec, time_mul);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        reap_children();
    }

    // Send SIGUSR2 again to the process group as backup.
    // Children will inherit SIG_IGN for SIGUSR2,
    // and they may have installed their handlers after the dyrektor signal.
    killpg(getpgrp(), SIGUSR2);

    // Wait for all children to exit.
    while (true) {
        int status = 0;
        pid_t wpid = waitpid(-1, &status, 0);
        if (wpid == -1) {
            if (errno == ECHILD) break; // no more children
            if (errno == EINTR) continue;
            break;
        }
    }

    ipc::shm::detach(shared_state);
    Logger::log(LogSeverity::Info, Identity::Generator, "Generator petentow zakonczyl prace.");
    return 0;
}
