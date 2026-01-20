#ifndef SO_PROJEKT_PROCESS_MANAGER_H
#define SO_PROJEKT_PROCESS_MANAGER_H

#include <sys/types.h>
#include <vector>
#include "../common.h"

namespace process {

struct UrzednikProcess {
    pid_t pid;
    UrzednikRole role;
};

struct UrzednikQueue {
    int msg_id;
    UrzednikRole role;
    int count;
};

pid_t spawn_rejestracja();
void terminate_rejestracja(pid_t pid);

bool spawn_rejestracja_group(std::vector<pid_t>& rejestracja_pids);
bool spawn_urzednicy(std::vector<UrzednikProcess>& urzednik_pids);

void stop_daily_rejestracja(std::vector<pid_t>& rejestracja_pids);
void stop_daily_urzednik(std::vector<UrzednikProcess>& urzednik_pids);

void terminate_rejestracja_all(std::vector<pid_t>& rejestracja_pids);
void terminate_urzednik_all(std::vector<UrzednikProcess>& urzednik_pids);

void send_rejestracja_shutdown(int msg_req_id, int count);
void send_urzednik_shutdowns(const std::vector<UrzednikQueue>& queues);

} // namespace process

#endif // SO_PROJEKT_PROCESS_MANAGER_H
