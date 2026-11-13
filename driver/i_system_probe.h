#pragma once

#include "model/system_facts.h"

namespace prts::driver {

// Reads machine state. Each domain is collected separately so that the
// self-check can time individual probes the way a real POST would.
class ISystemProbe {
public:
    virtual ~ISystemProbe() = default;

    virtual void collect(model::ProbeDomain domain, model::SystemFacts& facts) const = 0;
};

}  // namespace prts::driver
