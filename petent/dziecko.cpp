#include "dziecko.h"
#include <string>
#include <unistd.h>
#include "../ipcutils.h"
#include "../logger.h"

// Child thread entry point
static void* child_thread_func(void* arg) {
    auto* data = static_cast<ChildThreadData*>(arg);

    Logger::log(LogSeverity::Info, Identity::Petent,
                "Dziecko (rodzic PID: " + std::to_string(data->parent_pid) +
                ") wchodzi do urzedu z rodzicem.");

    ipc::mutex::lock(&data->mutex);
    while (!data->done && !(*data->evacuating)) {
        ipc::cond::wait(&data->cond, &data->mutex);
    }

    if (*data->evacuating) {
        Logger::log(LogSeverity::Notice, Identity::Petent,
                    "Dziecko (rodzic PID: " + std::to_string(data->parent_pid) +
                    ") - ewakuacja z rodzicem.");
    } else {
        Logger::log(LogSeverity::Info, Identity::Petent,
                    "Dziecko (rodzic PID: " + std::to_string(data->parent_pid) +
                    ") opuszcza urzad z rodzicem.");
    }
    ipc::mutex::unlock(&data->mutex);

    return nullptr;
}

void child_init(ChildThreadData* data, pid_t parent_pid, volatile sig_atomic_t* evacuating) {
    data->done = false;
    data->parent_pid = parent_pid;
    data->evacuating = evacuating;
    ipc::mutex::init(&data->mutex);
    ipc::cond::init(&data->cond);
}

void child_start(ChildThreadData* data, pthread_t* thread) {
    ipc::thread::create(thread, child_thread_func, data);
}

void child_signal_done(ChildThreadData* data) {
    ipc::mutex::lock(&data->mutex);
    data->done = true;
    ipc::cond::signal(&data->cond);
    ipc::mutex::unlock(&data->mutex);
}

void child_join_and_cleanup(ChildThreadData* data, pthread_t thread) {
    ipc::thread::join(thread);
    ipc::mutex::destroy(&data->mutex);
    ipc::cond::destroy(&data->cond);
}
