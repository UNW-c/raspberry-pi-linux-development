#pragma once

#include <string>

#include "service/camera_web_service.h"
#include "utils/logger.h"

namespace prts::controller {

inline constexpr const char* kWebVersion = "0.1.0";

// Command line front end of the camera web viewer.
class WebController {
public:
    struct Options {
        std::string bindAddress{"127.0.0.1"};
        int port{8080};
        std::string mqttHost{"127.0.0.1"};
        int mqttPort{1883};
        std::string topic{"prts/camera/frame"};
        std::string stampTopic{"prts/camera/stamp"};
        std::string clientId{"prts-web"};
        bool showHelp{false};
        bool showVersion{false};
    };

    WebController(service::CameraWebService& web, utils::Logger& logger);

    int run(int argc, const char* const argv[]);

    static Options parse(int argc, const char* const argv[], std::string& error);
    static void printHelp();

private:
    service::CameraWebService& web_;
    utils::Logger& logger_;
};

}  // namespace prts::controller
