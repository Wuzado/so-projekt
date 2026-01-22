#include "process.h"
#include <cerrno>
#include <string>
#include <vector>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../ipcutils.h"

namespace process {

    static std::vector<std::string> build_common_args(const char* role, const ProcessConfig& config) {
        std::vector<std::string> args;
        args.reserve(14);
        args.emplace_back("so_projekt");
        args.emplace_back("--role");
        args.emplace_back(role);
        args.emplace_back("--Tp");
        args.emplace_back(std::to_string(config.hours_open.first));
        args.emplace_back("--Tk");
        args.emplace_back(std::to_string(config.hours_open.second));
        args.emplace_back("--X1");
        args.emplace_back(std::to_string(config.department_limits[0]));
        args.emplace_back("--X2");
        args.emplace_back(std::to_string(config.department_limits[1]));
        args.emplace_back("--X3");
        args.emplace_back(std::to_string(config.department_limits[2]));
        args.emplace_back("--X4");
        args.emplace_back(std::to_string(config.department_limits[3]));
        args.emplace_back("--X5");
        args.emplace_back(std::to_string(config.department_limits[4]));
        return args;
    }

    static int exec_with_args(std::vector<std::string>& args) {
        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        execv("/proc/self/exe", argv.data());
        return -1;
    }

    static pid_t spawn_urzednik(UrzednikRole role, const ProcessConfig& config) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            return -1;
        }
        if (pid == 0) {
            auto dept_opt = urzednik_role_to_string(role);
            if (!dept_opt) {
                perror("invalid urzednik role");
                _exit(1);
            }
            const char* dept = dept_opt->data();

            std::vector<std::string> args = build_common_args("urzednik", config);
            args.emplace_back("--dept");
            args.emplace_back(dept);

            exec_with_args(args);
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

    pid_t spawn_rejestracja(const ProcessConfig& config) {
        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
            return -1;
        }
        if (pid == 0) {
            std::vector<std::string> args = build_common_args("rejestracja", config);
            exec_with_args(args);
            perror("exec failed");
            _exit(1);
        }
        return pid;
    }

    void terminate_rejestracja(pid_t pid) {
        if (pid <= 0) {
            return;
        }
        waitpid(pid, nullptr, 0);
    }

    bool spawn_rejestracja_group(std::vector<pid_t>& rejestracja_pids, const ProcessConfig& config) {
        pid_t first_pid = spawn_rejestracja(config);
        if (first_pid == -1) {
            return false;
        }
        rejestracja_pids.clear();
        rejestracja_pids.reserve(3);
        rejestracja_pids.push_back(first_pid);
        return true;
    }

    static void request_stop_after_current(pid_t pid) {
        if (pid <= 0) {
            return;
        }
        if (kill(pid, SIGUSR1) == -1) {
            perror("kill failed");
        }
    }

    bool spawn_urzednicy(std::vector<UrzednikProcess>& urzednik_pids, const ProcessConfig& config) {
        std::vector<UrzednikProcess> spawned;
        spawned.reserve(6);

        pid_t sa1 = spawn_urzednik(UrzednikRole::SA, config);
        if (sa1 != -1) {
            spawned.push_back({sa1, UrzednikRole::SA});
        }
        pid_t sa2 = spawn_urzednik(UrzednikRole::SA, config);
        if (sa2 != -1) {
            spawned.push_back({sa2, UrzednikRole::SA});
        }
        pid_t sc = spawn_urzednik(UrzednikRole::SC, config);
        if (sc != -1) {
            spawned.push_back({sc, UrzednikRole::SC});
        }
        pid_t km = spawn_urzednik(UrzednikRole::KM, config);
        if (km != -1) {
            spawned.push_back({km, UrzednikRole::KM});
        }
        pid_t ml = spawn_urzednik(UrzednikRole::ML, config);
        if (ml != -1) {
            spawned.push_back({ml, UrzednikRole::ML});
        }
        pid_t pd = spawn_urzednik(UrzednikRole::PD, config);
        if (pd != -1) {
            spawned.push_back({pd, UrzednikRole::PD});
        }

        if (sa1 == -1 || sa2 == -1 || sc == -1 || km == -1 || ml == -1 || pd == -1) {
            for (const auto& proc : spawned) {
                request_stop_after_current(proc.pid);
            }
            for (const auto& proc : spawned) {
                if (waitpid(proc.pid, nullptr, 0) == -1 && errno != ECHILD) {
                    perror("waitpid failed");
                }
            }
            return false;
        }

        urzednik_pids = std::move(spawned);
        return true;
    }

    void stop_daily_rejestracja(std::vector<pid_t>& rejestracja_pids) {
        for (pid_t pid : rejestracja_pids) {
            request_stop_after_current(pid);
        }
        for (pid_t pid : rejestracja_pids) {
            if (waitpid(pid, nullptr, 0) == -1 && errno != ECHILD) {
                perror("waitpid failed");
            }
        }
        rejestracja_pids.clear();
    }

    void stop_daily_urzednik(std::vector<UrzednikProcess>& urzednik_pids) {
        for (const auto& proc : urzednik_pids) {
            request_stop_after_current(proc.pid);
        }
        for (const auto& proc : urzednik_pids) {
            if (waitpid(proc.pid, nullptr, 0) == -1 && errno != ECHILD) {
                perror("waitpid failed");
            }
        }
        urzednik_pids.clear();
    }

    void terminate_rejestracja_all(std::vector<pid_t>& rejestracja_pids) {
        for (pid_t pid : rejestracja_pids) {
            terminate_rejestracja(pid);
        }
        rejestracja_pids.clear();
    }

    void terminate_urzednik_all(std::vector<UrzednikProcess>& urzednik_pids) {
        for (const auto& proc : urzednik_pids) {
            terminate_urzednik(proc.pid);
        }
        urzednik_pids.clear();
    }

    void send_rejestracja_shutdown(int msg_req_id, int count) {
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

    void send_urzednik_shutdowns(const std::vector<UrzednikQueue>& queues) {
        for (const auto& queue : queues) {
            send_urzednik_shutdown(queue.msg_id, queue.role, queue.count);
        }
    }

} // namespace process
