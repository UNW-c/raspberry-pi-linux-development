#include "service/telemetry_service.h"

#include <string>

#include "utils/text.h"

namespace prts::service {
namespace {

std::string escapeJson(const std::string& text) {
    std::string escaped;
    escaped.reserve(text.size() + 8);
    for (char character : text) {
        switch (character) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (static_cast<unsigned char>(character) < 0x20) {
                    escaped += ' ';
                } else {
                    escaped += character;
                }
                break;
        }
    }
    return escaped;
}

std::string temperatureField(double celsius) {
    if (celsius < 0.0) {
        return "null";
    }
    return utils::Text::formatFloat(celsius, 1);
}

}  // namespace

TelemetryService::TelemetryService(const driver::ISystemProbe& probe, const driver::IClock& clock)
    : probe_(probe), clock_(clock) {}

std::string TelemetryService::buildPayload(const model::SystemFacts& facts,
                                           const std::string& timestamp) {
    std::string payload;
    payload.reserve(256);
    payload += "{";
    payload += "\"ts\":\"" + escapeJson(timestamp) + "\",";
    payload += "\"host\":\"" + escapeJson(facts.hostname) + "\",";
    payload += "\"kernel\":\"" + escapeJson(facts.kernelRelease) + "\",";
    payload += "\"uptime_s\":" + utils::Text::formatFloat(facts.uptimeSeconds, 1) + ",";
    payload += "\"cpu_temp_c\":" + temperatureField(facts.cpuTemperatureC) + ",";
    payload += "\"mem_used_pct\":" + utils::Text::formatFloat(facts.memoryUsedPercent(), 1) + ",";
    payload += "\"disk_used_pct\":" + utils::Text::formatFloat(facts.diskUsedPercent(), 1) + ",";
    payload += "\"load1\":" + utils::Text::formatFloat(facts.loadAverage1, 2);
    payload += "}";
    return payload;
}

std::string TelemetryService::sample() {
    model::SystemFacts facts;
    probe_.collect(model::ProbeDomain::Kernel, facts);
    probe_.collect(model::ProbeDomain::Host, facts);
    probe_.collect(model::ProbeDomain::Cpu, facts);
    probe_.collect(model::ProbeDomain::Memory, facts);
    probe_.collect(model::ProbeDomain::Storage, facts);
    probe_.collect(model::ProbeDomain::Network, facts);
    probe_.collect(model::ProbeDomain::Session, facts);
    return buildPayload(facts, clock_.wallClock("%Y-%m-%dT%H:%M:%S"));
}

}  // namespace prts::service
