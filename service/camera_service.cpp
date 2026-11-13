#include "service/camera_service.h"

#include <string>

#include "utils/text.h"

namespace prts::service {

CameraService::CameraService(driver::ICameraSource& source, driver::IClock& clock)
    : source_(source), clock_(clock) {}

bool CameraService::open(const model::CameraConfig& config, std::string& error) {
    return source_.open(config, error);
}

bool CameraService::grab(model::CameraFrame& frame, int timeoutMs, std::string& error) {
    if (!source_.grab(frame, timeoutMs, error)) {
        return false;
    }
    if (frame.timestampMs == 0) {
        frame.timestampMs = static_cast<std::uint64_t>(clock_.steadyMillis());
    }
    return true;
}

void CameraService::close() { source_.close(); }

std::string CameraService::metaJson(const model::CameraFrame& frame, const std::string& timestamp) {
    std::string payload;
    payload.reserve(128);
    payload += "{";
    payload += "\"seq\":" + std::to_string(frame.sequence) + ",";
    payload += "\"bytes\":" + std::to_string(frame.data.size()) + ",";
    payload += "\"format\":\"" + frame.format + "\",";
    payload += "\"width\":" + std::to_string(frame.width) + ",";
    payload += "\"height\":" + std::to_string(frame.height) + ",";
    payload += "\"ts\":\"" + timestamp + "\"";
    payload += "}";
    return payload;
}

}  // namespace prts::service
