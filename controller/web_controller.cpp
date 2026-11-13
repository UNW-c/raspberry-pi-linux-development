#include "controller/web_controller.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace prts::controller {
namespace {

const char* environmentOrNull(const char* name) {
    const char* value = std::getenv(name);
    return (value == nullptr || value[0] == '\0') ? nullptr : value;
}

}  // namespace

WebController::WebController(service::CameraWebService& web, utils::Logger& logger)
    : web_(web), logger_(logger) {}

void WebController::printHelp() {
    std::printf(
        "prts-web %s - camera stream as a web page\n"
        "\n"
        "Usage: prts-web [options]\n"
        "\n"
        "Options:\n"
        "  --bind ADDRESS     listen address (default 127.0.0.1)\n"
        "  --port PORT        listen port (default 8080)\n"
        "  --mqtt-host HOST   broker address (default 127.0.0.1)\n"
        "  --mqtt-port PORT   broker port (default 1883)\n"
        "  --topic TOPIC      frame topic (default prts/camera/frame)\n"
        "  --stamp-topic T    capture timestamp topic used for latency (default prts/camera/stamp)\n"
        "  --client-id ID     MQTT client id (default prts-web)\n"
        "  -h, --help         show this help\n"
        "  -V, --version      print the version\n"
        "\n"
        "Routes: / (page), /stream (MJPEG), /snapshot (single JPEG), /status (JSON)\n"
        "Remote access: keep the default bind address and forward the port over SSH, e.g.\n"
        "  ssh -L 8080:127.0.0.1:8080 seven@<node>\n",
        kWebVersion);
}

WebController::Options WebController::parse(int argc, const char* const argv[],
                                            std::string& error) {
    Options options;
    error.clear();

    if (const char* host = environmentOrNull("PRTS_MQTT_HOST"); host != nullptr) {
        options.mqttHost = host;
    }
    if (const char* port = environmentOrNull("PRTS_MQTT_PORT"); port != nullptr) {
        options.mqttPort = std::atoi(port);
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        if (argument == "-h" || argument == "--help") {
            options.showHelp = true;
        } else if (argument == "-V" || argument == "--version") {
            options.showVersion = true;
        } else if (argument == "--bind" || argument == "--port" || argument == "--mqtt-host" ||
                   argument == "--mqtt-port" || argument == "--topic" ||
                   argument == "--stamp-topic" || argument == "--client-id") {
            if (index + 1 >= argc) {
                error = argument + " requires a value";
                return options;
            }
            const std::string value = argv[++index];
            if (argument == "--bind") {
                options.bindAddress = value;
            } else if (argument == "--port") {
                options.port = std::atoi(value.c_str());
            } else if (argument == "--mqtt-host") {
                options.mqttHost = value;
            } else if (argument == "--mqtt-port") {
                options.mqttPort = std::atoi(value.c_str());
            } else if (argument == "--topic") {
                options.topic = value;
            } else if (argument == "--stamp-topic") {
                options.stampTopic = value;
            } else {
                options.clientId = value;
            }
        } else {
            error = "unknown argument: " + argument;
            return options;
        }
    }
    return options;
}

int WebController::run(int argc, const char* const argv[]) {
    std::string error;
    const Options options = parse(argc, argv, error);
    if (!error.empty()) {
        std::fprintf(stderr, "prts-web: %s\n", error.c_str());
        return 2;
    }
    if (options.showHelp) {
        printHelp();
        return 0;
    }
    if (options.showVersion) {
        std::printf("prts-web %s\n", kWebVersion);
        return 0;
    }

    service::CameraWebService::Options serviceOptions;
    serviceOptions.bindAddress = options.bindAddress;
    serviceOptions.port = options.port;
    serviceOptions.mqttHost = options.mqttHost;
    serviceOptions.mqttPort = options.mqttPort;
    serviceOptions.frameTopic = options.topic;
    serviceOptions.stampTopic = options.stampTopic;
    serviceOptions.clientId = options.clientId;

    logger_.info("web viewer on " + options.bindAddress + ":" + std::to_string(options.port) +
                 " frames from " + options.mqttHost + ":" + std::to_string(options.mqttPort) +
                 " " + options.topic);
    return web_.run(serviceOptions);
}

}  // namespace prts::controller
