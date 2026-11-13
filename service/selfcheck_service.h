#pragma once

#include <vector>

#include "driver/i_clock.h"
#include "driver/i_system_probe.h"
#include "model/check_result.h"
#include "model/system_facts.h"
#include "utils/logger.h"

namespace prts::service {

// Turns raw machine facts into the ordered self-check table shown on screen.
// Each probe is timed individually, the way a POST reports device init cost.
class SelfcheckService {
public:
    SelfcheckService(const driver::ISystemProbe& probe, const driver::IClock& clock,
                     utils::Logger& logger);

    std::vector<model::CheckResult> run(model::SystemFacts& facts) const;

    static int countByStatus(const std::vector<model::CheckResult>& results,
                             model::CheckStatus status);

private:
    const driver::ISystemProbe& probe_;
    const driver::IClock& clock_;
    utils::Logger& logger_;
};

}  // namespace prts::service
