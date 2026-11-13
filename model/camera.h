#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prts::model {

// What to ask the capture device for. UVC cameras expose MJPG on most models,
// which keeps the payload small enough to push through MQTT as a single frame.
struct CameraConfig {
    std::string device{"/dev/video0"};
    int width{640};
    int height{480};
    std::string pixelFormat{"MJPG"};
    int fps{10};
};

// One captured frame, already in its wire format (JPEG for MJPG).
struct CameraFrame {
    std::vector<std::uint8_t> data;
    std::uint32_t sequence{0};
    std::uint64_t timestampMs{0};
    int width{0};
    int height{0};
    std::string format{"MJPG"};
    // Completed frames thrown away in favour of a newer one, so a slow reader
    // never replays old pictures.
    int drained{0};
};

}  // namespace prts::model
