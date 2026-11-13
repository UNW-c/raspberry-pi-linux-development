#pragma once

#include <string>

#include "driver/i_mqtt_client.h"
#include "service/telemetry_service.h"
#include "utils/logger.h"

namespace prts::controller {

inline constexpr const char* kMqttVersion = "0.1.0";

// Command line front end of the MQTT link: publish telemetry or subscribe to a
// topic. Arguments override the defaults from the environment.
class MqttController {
public:
    enum class Mode {
        Publish,
        Subscribe,
    };

    struct Options {
        Mode mode{Mode::Publish};
        std::string host{"127.0.0.1"};
        int port{1883};
        std::string topic{"prts/telemetry"};
        std::string clientId{"prts-node"};
        int count{0};  // 0 publishes/subscribes until interrupted
        int intervalMs{1000};
        bool showHelp{false};
        bool showVersion{false};
    };

    MqttController(driver::IMqttClient& client, service::TelemetryService& telemetry,
                   utils::Logger& logger);

    int run(int argc, const char* const argv[]);

    static Options parse(int argc, const char* const argv[], std::string& error);
    static void printHelp();

private:
    int runPublisher(const Options& options);
    int runSubscriber(const Options& options);

    driver::IMqttClient& client_;
    service::TelemetryService& telemetry_;
    utils::Logger& logger_;
};

}  // namespace prts::controller
