#pragma once

#include <string>

#include "driver/i_mqtt_client.h"
#include "utils/logger.h"

namespace prts::service {

// Serves the camera as a web page: subscribes to the MQTT frame topic on the
// node and re-publishes it as an MJPEG stream for browsers. Bound to loopback by
// default, so it is reached through an SSH local forward (which also works when
// the SSH port is exposed through a tunnel).
class CameraWebService {
public:
    struct Options {
        std::string bindAddress{"127.0.0.1"};
        int port{8080};
        std::string mqttHost{"127.0.0.1"};
        int mqttPort{1883};
        std::string frameTopic{"prts/camera/frame"};
        std::string stampTopic{"prts/camera/stamp"};
        std::string clientId{"prts-web"};
    };

    CameraWebService(driver::IMqttClient& mqtt, utils::Logger& logger);

    // Blocks until SIGINT/SIGTERM.
    int run(const Options& options);

    static const char* pageHtml();

private:
    driver::IMqttClient& mqtt_;
    utils::Logger& logger_;
};

}  // namespace prts::service
