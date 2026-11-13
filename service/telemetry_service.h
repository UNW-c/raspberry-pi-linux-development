#pragma once

#include <string>

#include "driver/i_clock.h"
#include "driver/i_system_probe.h"
#include "model/system_facts.h"

namespace prts::service {

// Builds the telemetry payload published over MQTT. Kept separate from the
// transport so the same JSON can later feed a camera stream or a web page.
class TelemetryService {
public:
    TelemetryService(const driver::ISystemProbe& probe, const driver::IClock& clock);

    // Probes the machine and returns one JSON document.
    std::string sample();

    // Pure helper, used by tests: serialises the facts as JSON.
    static std::string buildPayload(const model::SystemFacts& facts, const std::string& timestamp);

private:
    const driver::ISystemProbe& probe_;
    const driver::IClock& clock_;
};

}  // namespace prts::service
