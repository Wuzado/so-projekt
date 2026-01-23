#include "clock.h"
#include <format>
#include <pthread.h>
#include <unistd.h>
#include "../common.h"
#include "../ipcutils.h"
#include "../logger.h"

std::atomic<bool> simulation_running(true);
static pthread_mutex_t restart_mutex;
static pthread_cond_t restart_cv;
static bool restart_pending = false;
static bool restart_mutex_initialized = false;
static bool restart_cond_initialized = false;

struct ClockArgsHelper {
    SharedState* state;
    HoursOpen hours_open;
};

void init_clock(SharedState* state, HoursOpen hours_open) {
    while (simulation_running) {
        if (ipc::mutex::lock(&restart_mutex) == -1) {
            Logger::log(LogSeverity::Err, Identity::Dyrektor, "Blad blokady mutexu restartu dnia.");
            break;
        }
        while (restart_pending && simulation_running.load()) {
            if (ipc::cond::wait(&restart_cv, &restart_mutex) == -1) {
                Logger::log(LogSeverity::Err, Identity::Dyrektor, "Blad oczekiwania na restart dnia.");
                break;
            }
        }
        if (ipc::mutex::unlock(&restart_mutex) == -1) {
            Logger::log(LogSeverity::Err, Identity::Dyrektor, "Blad odblokowania mutexu restartu dnia.");
            break;
        }

        if (!simulation_running.load()) {
            break;
        }

        state->simulated_time = hours_open.first * 3600;
        state->office_status = OfficeStatus::Open;

        std::string message = std::format("Dzien {}: Urzad otwarty.", state->day + 1);
        Logger::log(LogSeverity::Info, Identity::Dyrektor, message);

        bool wait_after_close = false;

        while (state->simulated_time < hours_open.second * 3600 + 120) {
            if (!simulation_running.load()) {
                break;
            }

            int time_mul = static_cast<int>(state->time_mul);
            if (time_mul <= 0) {
                time_mul = 1;
            }
            int sleep_us = 1'000'000 / time_mul;
            if (sleep_us <= 0) {
                sleep_us = 1;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(sleep_us));

            state->simulated_time++;

            if (state->simulated_time >= hours_open.second * 3600 && !wait_after_close) {
                state->office_status = OfficeStatus::Closed;
                Logger::log(LogSeverity::Info, Identity::Dyrektor, "Urząd zamknięty.");
                wait_after_close = true;
            }
        }

        if (ipc::mutex::lock(&restart_mutex) == -1) {
            Logger::log(LogSeverity::Err, Identity::Dyrektor, "Blad blokady mutexu restartu dnia.");
            break;
        }
        state->day++;
        restart_pending = true;
        if (ipc::mutex::unlock(&restart_mutex) == -1) {
            Logger::log(LogSeverity::Err, Identity::Dyrektor, "Blad odblokowania mutexu restartu dnia.");
            break;
        }
        ipc::cond::broadcast(&restart_cv);
        Logger::log(LogSeverity::Info, Identity::Dyrektor, "Koniec dnia.");
    }
}

static void* clock_thread_main(void* arg) {
    auto* args = static_cast<ClockArgsHelper*>(arg);
    init_clock(args->state, args->hours_open);
    delete args;
    return nullptr;
}

int start_clock(SharedState* shared_state, HoursOpen hours_open, pthread_t* out_thread) {
    if (!out_thread) {
        Logger::log(LogSeverity::Err, Identity::Dyrektor, "Niepoprawny wskaznik watku zegara.");
        return -1;
    }

    if (!restart_mutex_initialized) {
        if (ipc::mutex::init(&restart_mutex, false) == -1) {
            Logger::log(LogSeverity::Emerg, Identity::Dyrektor, "Nie udalo sie zainicjowac mutexu restartu dnia.");
            return -1;
        }
        restart_mutex_initialized = true;
    }

    if (!restart_cond_initialized) {
        if (ipc::cond::init(&restart_cv, false) == -1) {
            Logger::log(LogSeverity::Emerg, Identity::Dyrektor, "Nie udalo sie zainicjowac warunku restartu dnia.");
            return -1;
        }
        restart_cond_initialized = true;
    }

    auto* args = new ClockArgsHelper{shared_state, hours_open};

    error_t create_err = ipc::thread::create(out_thread, clock_thread_main, args);
    if (create_err != 0) {
        Logger::log(LogSeverity::Emerg, Identity::Dyrektor, "Nie udalo sie uruchomic watku zegara.");
        delete args;
        return -1;
    }

    return 0;
}

int stop_clock(pthread_t thread) {
    simulation_running = false;
    ipc::cond::broadcast(&restart_cv);
    Logger::log(LogSeverity::Info, Identity::Dyrektor, "Zatrzymywanie zegara symulacji...");
    error_t join_err = ipc::thread::join(thread);
    if (join_err != 0) {
        Logger::log(LogSeverity::Emerg, Identity::Dyrektor, "Nie udalo sie dolaczyc do watku zegara.");
        return -1;
    }
    return 0;
}

void notify_day_restart_complete() {
    if (ipc::mutex::lock(&restart_mutex) == -1) {
        Logger::log(LogSeverity::Err, Identity::Dyrektor, "Blad blokady mutexu restartu dnia.");
        return;
    }
    restart_pending = false;
    if (ipc::mutex::unlock(&restart_mutex) == -1) {
        Logger::log(LogSeverity::Err, Identity::Dyrektor, "Blad odblokowania mutexu restartu dnia.");
        return;
    }
    ipc::cond::broadcast(&restart_cv);
}
