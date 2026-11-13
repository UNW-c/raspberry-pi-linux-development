#pragma once

#include <cstdint>
#include <string>

namespace prts::driver {

// Time abstraction. Injecting it makes the animation deterministic in tests
// and keeps pacing decisions out of the drivers.
class IClock {
public:
    virtual ~IClock() = default;

    // Monotonic milliseconds, used for pacing and duration measurement.
    virtual std::int64_t steadyMillis() const = 0;

    // Wall clock in local time, formatted with strftime semantics.
    virtual std::string wallClock(const std::string& format) const = 0;

    // Local wall clock with millisecond precision, "HH:MM:SS.mmm". Used by the
    // self-check log, where each line carries the time it was emitted.
    virtual std::string timestampWithMillis() const = 0;

    virtual void sleepMillis(int milliseconds) = 0;
};

}  // namespace prts::driver
