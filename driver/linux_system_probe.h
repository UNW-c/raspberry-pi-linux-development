#pragma once

#include <string>

#include "driver/i_system_probe.h"

namespace prts::driver {

// Linux implementation. Every value is read from procfs/sysfs or a POSIX API;
// no external process is spawned, so probing costs microseconds.
class LinuxSystemProbe final : public ISystemProbe {
public:
    void collect(model::ProbeDomain domain, model::SystemFacts& facts) const override;

private:
    void collectKernel(model::SystemFacts& facts) const;
    void collectHost(model::SystemFacts& facts) const;
    void collectCpu(model::SystemFacts& facts) const;
    void collectMemory(model::SystemFacts& facts) const;
    void collectStorage(model::SystemFacts& facts) const;
    void collectNetwork(model::SystemFacts& facts) const;
    void collectSession(model::SystemFacts& facts) const;
};

}  // namespace prts::driver
