#pragma once

#include <string>

#include "model/camera.h"

namespace prts::driver {

// Video capture contract. Keeping it behind an interface lets the camera
// service be tested without a real device.
class ICameraSource {
public:
    virtual ~ICameraSource() = default;

    virtual bool open(const model::CameraConfig& config, std::string& error) = 0;
    virtual bool grab(model::CameraFrame& frame, int timeoutMs, std::string& error) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
};

}  // namespace prts::driver
