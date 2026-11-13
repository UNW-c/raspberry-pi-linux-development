#pragma once

#include <string>

#include "driver/i_camera_source.h"
#include "driver/i_clock.h"
#include "model/camera.h"

namespace prts::service {

// Business side of the camera link: owns the capture device lifecycle and turns
// a frame into the small metadata document that travels next to the image.
class CameraService {
public:
    CameraService(driver::ICameraSource& source, driver::IClock& clock);

    bool open(const model::CameraConfig& config, std::string& error);
    bool grab(model::CameraFrame& frame, int timeoutMs, std::string& error);
    void close();

    // {"seq":12,"bytes":20982,"format":"MJPG","width":640,"height":480,"ts":"..."}
    static std::string metaJson(const model::CameraFrame& frame, const std::string& timestamp);

private:
    driver::ICameraSource& source_;
    driver::IClock& clock_;
};

}  // namespace prts::service
