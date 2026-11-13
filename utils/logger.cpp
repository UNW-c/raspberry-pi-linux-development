#include "utils/logger.h"

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#include "utils/text.h"

namespace prts::utils {
namespace {

std::string timestampNow() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now.time_since_epoch()) %
                        std::chrono::seconds(1);
    std::tm local{};
    localtime_r(&seconds, &local);
    std::ostringstream stream;
    stream << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0')
           << std::setw(3) << millis.count();
    return stream.str();
}

const char* environmentOrEmpty(const char* name) {
    const char* value = std::getenv(name);
    return value == nullptr ? "" : value;
}

}  // namespace

Logger::Logger(std::string filePath, LogLevel minimum)
    : path_(std::move(filePath)), minimum_(minimum) {
    std::error_code error;
    const std::filesystem::path target(path_);
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), error);
    }
    stream_.open(path_, std::ios::app);
}

LogLevel Logger::levelFromName(std::string_view name) {
    const std::string lowered = Text::lower(name);
    if (lowered == "debug") {
        return LogLevel::Debug;
    }
    if (lowered == "info") {
        return LogLevel::Info;
    }
    if (lowered == "error") {
        return LogLevel::Error;
    }
    return LogLevel::Warn;
}

const char* Logger::nameOf(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warn:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }
    return "?";
}

std::string Logger::defaultLogPath() {
    if (const std::string custom = environmentOrEmpty("PRTS_LOG_FILE"); !custom.empty()) {
        return custom;
    }
    const std::string stateHome = environmentOrEmpty("XDG_STATE_HOME");
    if (!stateHome.empty()) {
        return stateHome + "/prts/prts-boot.log";
    }
    const std::string home = environmentOrEmpty("HOME");
    if (!home.empty()) {
        return home + "/.local/state/prts/prts-boot.log";
    }
    return "/tmp/prts-boot.log";
}

void Logger::write(LogLevel level, std::string_view message) {
    if (level < minimum_) {
        return;
    }
    std::lock_guard<std::mutex> guard(mutex_);
    if (!stream_.is_open()) {
        return;
    }
    stream_ << timestampNow() << " [" << nameOf(level) << "] " << message << '\n';
    stream_.flush();
}

void Logger::flush() {
    std::lock_guard<std::mutex> guard(mutex_);
    if (stream_.is_open()) {
        stream_.flush();
    }
}

}  // namespace prts::utils
