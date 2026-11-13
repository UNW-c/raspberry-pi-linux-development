#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace prts::utils {

enum class LogLevel {
    Debug,
    Info,
    Warn,
    Error,
};

// File backed logger. The boot sequence owns the terminal while it animates,
// so diagnostics never go to stdout/stderr: they land in a state file instead.
class Logger {
public:
    Logger(std::string filePath, LogLevel minimum = LogLevel::Warn);

    static LogLevel levelFromName(std::string_view name);
    static const char* nameOf(LogLevel level);
    static std::string defaultLogPath();

    void setMinimumLevel(LogLevel level) { minimum_ = level; }
    LogLevel minimumLevel() const { return minimum_; }

    void debug(std::string_view message) { write(LogLevel::Debug, message); }
    void info(std::string_view message) { write(LogLevel::Info, message); }
    void warn(std::string_view message) { write(LogLevel::Warn, message); }
    void error(std::string_view message) { write(LogLevel::Error, message); }

    void flush();

private:
    void write(LogLevel level, std::string_view message);

    std::mutex mutex_;
    std::string path_;
    std::ofstream stream_;
    LogLevel minimum_;
};

}  // namespace prts::utils
