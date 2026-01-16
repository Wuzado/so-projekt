#include "clock.h"
#include <format>
#include <pthread.h>
#include <unistd.h>
#include "../common.h"
#include "../ipcutils.h"
#include "../logger.h"

std::atomic<bool> simulation_running(true);

struct ClockArgsHelper {
    SharedState* state;
    HoursOpen hours_open;
};

void init_clock(SharedState* state, HoursOpen hours_open) {
    while (simulation_running) {
        state->simulated_time = hours_open.first * 3600;
        state->office_status = OfficeStatus::Open;

        std::string message = std::format("Dzien {}: Urzad otwarty.", state->day + 1);
        Logger::log(LogSeverity::Info, Identity::Dyrektor, message);

        while (state->simulated_time < hours_open.second * 3600 + 120) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1000 / TIME_MUL));

            state->simulated_time++;

            if (state->simulated_time >= hours_open.second * 3600) {
                state->office_status = OfficeStatus::Closed;
                Logger::log(LogSeverity::Info, Identity::Dyrektor, "Urząd zamknięty.");
            }
        }

        state->day++;
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
    Logger::log(LogSeverity::Info, Identity::Dyrektor, "Zatrzymywanie zegara symulacji...");
    error_t join_err = ipc::thread::join(thread);
    if (join_err != 0) {
        Logger::log(LogSeverity::Emerg, Identity::Dyrektor, "Nie udalo sie dolaczyc do watku zegara.");
        return -1;
    }
    return 0;
}
