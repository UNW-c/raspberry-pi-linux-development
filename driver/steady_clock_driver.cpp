#include "driver/steady_clock_driver.h"

#include <chrono>
#include <ctime>
#include <thread>

namespace prts::driver {

std::int64_t SteadyClockDriver::steadyMillis() const {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
}

std::string SteadyClockDriver::wallClock(const std::string& format) const {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[128] = {0};
    if (std::strftime(buffer, sizeof(buffer), format.c_str(), &local) == 0) {
        return std::string();
    }
    return std::string(buffer);
}

std::string SteadyClockDriver::timestampWithMillis() const {
    const auto now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[32] = {0};
    std::snprintf(buffer, sizeof(buffer), "%02d:%02d:%02d.%03d", local.tm_hour, local.tm_min,
                  local.tm_sec, static_cast<int>(millis.count()));
    return std::string(buffer);
}

void SteadyClockDriver::sleepMillis(int milliseconds) {
    if (milliseconds > 0) {
        std::this_thread::sleep_for(std::chrono::milliseconds(milliseconds));
    }
}

}  // namespace prts::driver
