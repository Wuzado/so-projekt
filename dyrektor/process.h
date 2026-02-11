#ifndef SO_PROJEKT_PROCESS_MANAGER_H
#define SO_PROJEKT_PROCESS_MANAGER_H

#include <array>
#include <sys/types.h>
#include <vector>
#include "../common.h"

namespace process {

struct ProcessConfig {
    HoursOpen hours_open;
    std::array<uint32_t, 5> department_limits;
    int time_mul;
    int gen_min_delay_sec;
    int gen_max_delay_sec;
    int gen_max_count;
    bool one_day;
    int building_capacity;
};

struct UrzednikProcess {
    pid_t pid;
    UrzednikRole role;
};

struct UrzednikQueue {
    int msg_id;
    UrzednikRole role;
    int count;
};

pid_t spawn_rejestracja(const ProcessConfig& config);
pid_t spawn_generator(const ProcessConfig& config);
void wait_rejestracja(pid_t pid);
void terminate_generator(pid_t pid);

bool spawn_rejestracja_group(std::vector<pid_t>& rejestracja_pids, const ProcessConfig& config);
bool spawn_urzednicy(std::vector<UrzednikProcess>& urzednik_pids, const ProcessConfig& config);

void stop_daily_rejestracja(std::vector<pid_t>& rejestracja_pids);
void stop_daily_urzednik(std::vector<UrzednikProcess>& urzednik_pids);

void wait_rejestracja_all(std::vector<pid_t>& rejestracja_pids);
void wait_urzednik_all(std::vector<UrzednikProcess>& urzednik_pids);

void send_rejestracja_shutdown(int msg_req_id, int count);
void send_urzednik_shutdowns(const std::vector<UrzednikQueue>& queues);

namespace group {
    int init_self();
    int signal_self(int signal);
}

} // namespace process

#endif // SO_PROJEKT_PROCESS_MANAGER_H
