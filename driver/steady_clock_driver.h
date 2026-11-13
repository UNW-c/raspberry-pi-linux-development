#pragma once

#include "driver/i_clock.h"

namespace prts::driver {

class SteadyClockDriver final : public IClock {
public:
    std::int64_t steadyMillis() const override;
    std::string wallClock(const std::string& format) const override;
    std::string timestampWithMillis() const override;
    void sleepMillis(int milliseconds) override;
};

}  // namespace prts::driver
