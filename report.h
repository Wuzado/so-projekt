#ifndef SO_PROJEKT_REPORT_H
#define SO_PROJEKT_REPORT_H

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <unistd.h>
#include "common.h"

namespace report {

inline std::string report_path(uint32_t day_number) {
    return "/tmp/so_projekt_report_day_" + std::to_string(day_number) + ".txt";
}

inline int append_line(uint32_t day_number, std::string_view line) {
    std::string path = report_path(day_number);
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        perror("open report file failed");
        return -1;
    }

    if (flock(fd, LOCK_EX) == -1) {
        perror("flock report file failed");
        close(fd);
        return -1;
    }

    std::string line_with_newline(line);
    line_with_newline.push_back('\n');
    if (write(fd, line_with_newline.c_str(), line_with_newline.size()) == -1) {
        perror("write report file failed");
        flock(fd, LOCK_UN);
        close(fd);
        return -1;
    }

    if (flock(fd, LOCK_UN) == -1) {
        perror("flock unlock report file failed");
        close(fd);
        return -1;
    }

    if (close(fd) == -1) {
        perror("close report file failed");
        return -1;
    }

    return 0;
}

inline void log_unserved_after_close(uint32_t day_number, uint32_t petent_id, UrzednikRole dept,
                                     uint32_t ticket_number) {
    auto dept_str = urzednik_role_to_string(dept);
    std::string dept_text = dept_str ? std::string(*dept_str) : "NIEZNANY";
    std::string line = std::to_string(petent_id) +
                       " - sprawa do " + dept_text +
                       " - nr biletu " + std::to_string(ticket_number);
    append_line(day_number, line);
}

inline void log_unserved_after_signal(uint32_t day_number, uint32_t petent_id, UrzednikRole dept,
                                      std::string_view issuer) {
    auto dept_str = urzednik_role_to_string(dept);
    std::string dept_text = dept_str ? std::string(*dept_str) : "NIEZNANY";
    std::string line = std::to_string(petent_id) +
                       " - skierowanie do " + dept_text +
                       " - wystawil " + std::string(issuer);
    append_line(day_number, line);
}

} // namespace report

#endif // SO_PROJEKT_REPORT_H