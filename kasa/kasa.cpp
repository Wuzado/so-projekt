#include "kasa.h"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#include "../ipcutils.h"
#include "../logger.h"

static volatile sig_atomic_t kasa_running = 1;
static volatile sig_atomic_t stop_after_current = 0;

static void handle_shutdown_signal(int) { kasa_running = 0; }

static void handle_finish_signal(int) { stop_after_current = 1; }

static void payment_delay(int time_mul) {
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

int kasa_main() {
    ipc::install_signal_handler(SIGTERM, handle_shutdown_signal);
    ipc::install_signal_handler(SIGUSR1, handle_finish_signal);
    signal(SIGINT, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    Logger::log(LogSeverity::Info, Identity::Kasa, "Kasa uruchomiona.");

    auto shared_state = ipc::helper::get_shared_state(true);
    if (!shared_state) {
        return 1;
    }

    int msg_kasa_id = ipc::helper::get_msg_queue(ipc::KeyType::MsgQueueKasa);
    if (msg_kasa_id == -1) {
        Logger::log(LogSeverity::Err, Identity::Kasa, "Nie znaleziono kolejki kasy.");
        ipc::shm::detach(shared_state);
        return 1;
    }

    int msg_req_id = ipc::helper::get_msg_queue(ipc::KeyType::MsgQueueRejestracja);
    if (msg_req_id == -1) {
        Logger::log(LogSeverity::Err, Identity::Kasa, "Nie znaleziono kolejki rejestracji.");
        ipc::shm::detach(shared_state);
        return 1;
    }

    while (kasa_running) {
        KasaRequestMsg request{};
        int rc = ipc::msg::receive<KasaRequestMsg>(msg_kasa_id, kKasaRequestType, &request, 0);
        if (rc == -1) {
            if (errno == EINTR) {
                if (!kasa_running || stop_after_current) {
                    break;
                }
                continue;
            }
            std::string error = "Blad odbioru zadania oplaty: " + std::string(std::strerror(errno));
            Logger::log(LogSeverity::Err, Identity::Kasa, error);
            continue;
        }

        // Sentinel message (petent_id == 0) signals shutdown
        if (request.petent_id == 0) {
            Logger::log(LogSeverity::Notice, Identity::Kasa, "Otrzymano sygnal zakonczenia.");
            break;
        }

        Logger::log(LogSeverity::Info, Identity::Kasa,
                    "Petent " + std::to_string(request.petent_id) + " dokonuje oplaty.");

        payment_delay(static_cast<int>(shared_state->time_mul));

        // Send payment confirmation to petitioner via rejestracja queue (mtype = petent_id)
        ServiceDoneMsg done{};
        done.petent_id = request.petent_id;
        done.department = request.department;
        done.action = ServiceAction::Complete;

        int send_flags = stop_after_current ? IPC_NOWAIT : 0;
        if (ipc::msg::send<ServiceDoneMsg>(msg_req_id, static_cast<long>(request.petent_id), done, send_flags) == -1) {
            Logger::log(LogSeverity::Err, Identity::Kasa,
                        "Blad wyslania potwierdzenia oplaty dla petenta " + std::to_string(request.petent_id) + ".");
        } else {
            Logger::log(LogSeverity::Info, Identity::Kasa,
                        "Petent " + std::to_string(request.petent_id) + " zakonczyl oplate.");
        }

        if (stop_after_current) {
            break;
        }
    }

    ipc::shm::detach(shared_state);
    Logger::log(LogSeverity::Info, Identity::Kasa, "Kasa zakonczona.");
    return 0;
}
