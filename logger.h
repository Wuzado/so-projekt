#ifndef SO_PROJEKT_LOGGER_H
#define SO_PROJEKT_LOGGER_H

#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/file.h>
#include <thread>
#include <unistd.h>
#include "common.h"

constexpr bool LOG_TO_STDOUT = true;

enum class LogSeverity { Emerg, Alert, Crit, Err, Warning, Notice, Info, Debug };

class Logger {
private:
    inline static std::string log_file_path = "./so_projekt.log";

    static constexpr std::string_view severity_to_string(LogSeverity severity) noexcept {
        switch (severity) {
            case LogSeverity::Emerg:
                return "EMERG";
            case LogSeverity::Alert:
                return "ALERT";
            case LogSeverity::Crit:
                return "CRIT";
            case LogSeverity::Err:
                return "ERR";
            case LogSeverity::Warning:
                return "WARNING";
            case LogSeverity::Notice:
                return "NOTICE";
            case LogSeverity::Info:
                return "INFO";
            case LogSeverity::Debug:
                return "DEBUG";
            default:
                return "UNKNOWN";
        }
    }

    static constexpr std::string_view identity_to_string(Identity identity) noexcept {
        switch (identity) {
            case Identity::Petent:
                return "PETENT";
            case Identity::Urzednik:
                return "URZEDNIK";
            case Identity::Dyrektor:
                return "DYREKTOR";
            case Identity::Rejestracja:
                return "REJESTRACJA";
            case Identity::Generator:
                return "GENERATOR";
            case Identity::Kasa:
                return "KASA";
            default:
                return "UNKNOWN";
        }
    }

    static constexpr std::string_view role_to_string(UrzednikRole role) noexcept {
        switch (role) {
            case UrzednikRole::SC:
                return "SC";
            case UrzednikRole::KM:
                return "KM";
            case UrzednikRole::ML:
                return "ML";
            case UrzednikRole::PD:
                return "PD";
            case UrzednikRole::SA:
                return "SA";
            default:
                return "UNKNOWN";
        }
    }

    // Get it in ISO 8601 format w/ offset
    static std::string get_current_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);

        std::tm timeinfo{};
        localtime_r(&time_t_now, &timeinfo);

        std::ostringstream ss;
        ss << std::put_time(&timeinfo, "%Y-%m-%dT%H:%M:%S");

        char tz_offset[10];
        std::strftime(tz_offset, sizeof(tz_offset), "%z", &timeinfo);

        if (strlen(tz_offset) >= 5) {
            ss << tz_offset[0] << tz_offset[1] << tz_offset[2] << ':' << tz_offset[3] << tz_offset[4];
        }

        return ss.str();
    }

    static void write_log(const std::string& message, bool to_stdout = LOG_TO_STDOUT) {
        if (to_stdout) {
            fflush(stdout);
            int fd = STDOUT_FILENO;

            // Try to lock stdout if possible; ignore lock failures and still write
            // Realistically, any messages under PIPE_BUF (4KB on modern Linux) will be atomic anyway
            flock(fd, LOCK_EX);

            ssize_t bytes_written = write(fd, message.c_str(), message.length());
            if (bytes_written == -1) {
                perror("Failed to write to stdout");
            }

            flock(fd, LOCK_UN);
        }

        int fd = open(log_file_path.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd == -1) {
            perror("Failed to open log file");
            return;
        }

        if (flock(fd, LOCK_EX) == -1) {
            perror("Failed to lock log file");
            close(fd);
            return;
        }

        ssize_t bytes_written = write(fd, message.c_str(), message.length());
        if (bytes_written == -1) {
            perror("Failed to write to log file");
        }

        // Unlock
        flock(fd, LOCK_UN);
        close(fd);
    }

public:
    // Identity only log
    static void log(LogSeverity severity, Identity identity, const std::string& message, bool to_stdout = LOG_TO_STDOUT) {
        std::ostringstream ss;
        ss << get_current_timestamp() << " "
           << "[PID:" << getpid() << "] "
           << "[TID:" << std::this_thread::get_id() << "] " << severity_to_string(severity) << " "
           << identity_to_string(identity) << ": " << message << '\n';

        write_log(ss.str(), to_stdout);
    }

    // Identity and urzednik role log
    static void log(LogSeverity severity, Identity identity, UrzednikRole role, const std::string& message, bool to_stdout = LOG_TO_STDOUT) {
        std::ostringstream ss;
        ss << get_current_timestamp() << " "
           << "[PID:" << getpid() << "] "
           << "[TID:" << std::this_thread::get_id() << "] " << severity_to_string(severity) << " "
           << identity_to_string(identity) << "(" << role_to_string(role) << "): " << message << '\n';

        write_log(ss.str(), to_stdout);
    }

    // Set log file
    static void set_log_file(const std::string& path) { log_file_path = path; }

    // Clear log file
    static void clear_log() {
        int fd = open(log_file_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd == -1) {
            perror("Failed to clear log file");
            return;
        }
        close(fd);
    }
};

#endif // SO_PROJEKT_LOGGER_H
