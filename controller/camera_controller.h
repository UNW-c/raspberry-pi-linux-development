#pragma once

#include <string>

#include "driver/i_clock.h"
#include "driver/i_mqtt_client.h"
#include "service/camera_service.h"
#include "utils/logger.h"

namespace prts::controller {

inline constexpr const char* kCameraVersion = "0.1.0";

// Command line front end of the camera link: capture MJPEG frames and publish
// them one message per frame.
class CameraController {
public:
    struct Options {
        std::string host{"127.0.0.1"};
        int port{1883};
        std::string clientId{"prts-camera"};
        std::string frameTopic{"prts/camera/frame"};
        std::string metaTopic{"prts/camera/meta"};
        std::string stampTopic{"prts/camera/stamp"};
        std::string device{"/dev/video0"};
        int width{640};
        int height{480};
        int fps{30};
        int scale{4};      // 1 = full size, 2 = half, 4 = quarter
        int quality{50};
        int maxFrames{0};
        int metaEvery{30};
        bool showHelp{false};
        bool showVersion{false};
    };

    CameraController(driver::IMqttClient& client, service::CameraService& camera,
                     driver::IClock& clock, utils::Logger& logger);

    int run(int argc, const char* const argv[]);

    static Options parse(int argc, const char* const argv[], std::string& error);
    static void printHelp();

private:
    int stream(const Options& options);

    driver::IMqttClient& client_;
    service::CameraService& camera_;
    driver::IClock& clock_;
    utils::Logger& logger_;
};

}  // namespace prts::controller
