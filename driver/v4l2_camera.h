#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "driver/i_camera_source.h"
#include "model/camera.h"

namespace prts::driver {

// V4L2 capture using memory-mapped buffers. Only the single-planar VIDEO_CAPTURE
// interface is used, which is what UVC cameras expose.
class V4l2Camera final : public ICameraSource {
public:
    V4l2Camera() = default;
    ~V4l2Camera() override;

    V4l2Camera(const V4l2Camera&) = delete;
    V4l2Camera& operator=(const V4l2Camera&) = delete;

    bool open(const model::CameraConfig& config, std::string& error) override;
    bool grab(model::CameraFrame& frame, int timeoutMs, std::string& error) override;
    void close() override;
    bool isOpen() const override { return device_ >= 0; }

private:
    struct Buffer {
        void* start{nullptr};
        std::size_t length{0};
    };

    bool configure(const model::CameraConfig& config, std::string& error);
    bool startStreaming(std::string& error);
    void releaseBuffers();

    int device_{-1};
    std::vector<Buffer> buffers_;
    int width_{0};
    int height_{0};
    std::string format_{"MJPG"};
    std::uint32_t sequence_{0};
};

}  // namespace prts::driver
