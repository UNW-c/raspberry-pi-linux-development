#include <cstdint>
#include <string>
#include <vector>

#include "driver/i_camera_source.h"
#include "model/camera.h"
#include "service/camera_service.h"
#include "test/fakes.h"
#include "test/test_framework.h"

namespace {

// Camera that produces synthetic JPEG-looking bytes instead of touching /dev.
class FakeCameraSource : public prts::driver::ICameraSource {
public:
    bool open(const prts::model::CameraConfig& config, std::string& error) override {
        error.clear();
        opened_ = true;
        width_ = config.width;
        height_ = config.height;
        return true;
    }

    bool grab(prts::model::CameraFrame& frame, int, std::string& error) override {
        error.clear();
        if (!opened_) {
            error = "not open";
            return false;
        }
        frame.data = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 0xFF, 0xD9};
        frame.width = width_;
        frame.height = height_;
        frame.format = "MJPG";
        frame.sequence = sequence_++;
        frame.timestampMs = 0;
        return true;
    }

    void close() override { opened_ = false; }
    bool isOpen() const override { return opened_; }

private:
    bool opened_{false};
    int width_{0};
    int height_{0};
    std::uint32_t sequence_{0};
};

}  // namespace

PRTS_TEST(camera_service_forwards_frames) {
    FakeCameraSource source;
    prts::test::FakeClock clock;
    prts::service::CameraService service(source, clock);

    std::string error;
    prts::model::CameraConfig config;
    config.width = 1280;
    config.height = 720;
    PRTS_CHECK(service.open(config, error));

    prts::model::CameraFrame frame;
    PRTS_CHECK(service.grab(frame, 100, error));
    PRTS_CHECK_EQ(frame.data.size(), std::size_t{8});
    PRTS_CHECK_EQ(frame.width, 1280);
    PRTS_CHECK_EQ(frame.height, 720);
    PRTS_CHECK_EQ(frame.format, std::string("MJPG"));
    // The service stamps frames that the driver left untimed.
    PRTS_CHECK(frame.timestampMs != 0 || clock.steadyMillis() == 0);

    PRTS_CHECK(service.grab(frame, 100, error));
    PRTS_CHECK_EQ(frame.sequence, static_cast<std::uint32_t>(1));
}

PRTS_TEST(camera_meta_json_describes_the_frame) {
    prts::model::CameraFrame frame;
    frame.data = std::vector<std::uint8_t>(2048, 0x42);
    frame.sequence = 7;
    frame.width = 640;
    frame.height = 480;
    frame.format = "MJPG";

    const std::string json =
        prts::service::CameraService::metaJson(frame, "2026-11-13T21:20:00");
    PRTS_CHECK_EQ(json.front(), '{');
    PRTS_CHECK_EQ(json.back(), '}');
    PRTS_CHECK(json.find("\"seq\":7") != std::string::npos);
    PRTS_CHECK(json.find("\"bytes\":2048") != std::string::npos);
    PRTS_CHECK(json.find("\"width\":640") != std::string::npos);
    PRTS_CHECK(json.find("\"height\":480") != std::string::npos);
    PRTS_CHECK(json.find("\"format\":\"MJPG\"") != std::string::npos);
    PRTS_CHECK(json.find("\"ts\":\"2026-11-13T21:20:00\"") != std::string::npos);
}

PRTS_TEST(camera_service_reports_open_failure) {
    class BrokenCamera : public prts::driver::ICameraSource {
    public:
        bool open(const prts::model::CameraConfig&, std::string& error) override {
            error = "open /dev/video9: No such file or directory";
            return false;
        }
        bool grab(prts::model::CameraFrame&, int, std::string&) override { return false; }
        void close() override {}
        bool isOpen() const override { return false; }
    } camera;

    prts::test::FakeClock clock;
    prts::service::CameraService service(camera, clock);
    std::string error;
    PRTS_CHECK(!service.open(prts::model::CameraConfig(), error));
    PRTS_CHECK_EQ(error, std::string("open /dev/video9: No such file or directory"));
}
