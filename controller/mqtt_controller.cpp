#include "controller/mqtt_controller.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

namespace prts::controller {
namespace {

const char* environmentOr(const char* name, const std::string& fallback) {
    const char* value = std::getenv(name);
    return (value == nullptr || value[0] == '\0') ? nullptr : value;
}

}  // namespace

MqttController::MqttController(driver::IMqttClient& client, service::TelemetryService& telemetry,
                               utils::Logger& logger)
    : client_(client), telemetry_(telemetry), logger_(logger) {}

void MqttController::printHelp() {
    std::printf(
        "prts-mqtt %s - telemetry link for the PRTS node\n"
        "\n"
        "Usage: prts-mqtt <publish|subscribe> [options]\n"
        "\n"
        "  publish      sample the machine and publish one JSON message per tick\n"
        "  subscribe    print every message received on the topic filter\n"
        "\n"
        "Options:\n"
        "  --host HOST        broker address (default 127.0.0.1)\n"
        "  --port PORT        broker port (default 1883)\n"
        "  --topic TOPIC      topic to publish to, or filter to subscribe (default prts/telemetry)\n"
        "  --count N          stop after N messages (default 0 = until interrupted)\n"
        "  --interval MS      publish interval in milliseconds (default 1000)\n"
        "  --client-id ID     MQTT client id (default prts-node)\n"
        "  -h, --help         show this help\n"
        "  -V, --version      print the version\n"
        "\n"
        "Environment: PRTS_MQTT_HOST, PRTS_MQTT_PORT, PRTS_MQTT_TOPIC\n",
        kMqttVersion);
}

MqttController::Options MqttController::parse(int argc, const char* const argv[],
                                              std::string& error) {
    Options options;
    error.clear();

    if (const char* host = environmentOr("PRTS_MQTT_HOST", options.host); host != nullptr) {
        options.host = host;
    }
    if (const char* port = environmentOr("PRTS_MQTT_PORT", std::to_string(options.port));
        port != nullptr) {
        options.port = std::atoi(port);
    }
    if (const char* topic = environmentOr("PRTS_MQTT_TOPIC", options.topic); topic != nullptr) {
        options.topic = topic;
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        if (argument == "publish" || argument == "pub") {
            options.mode = Mode::Publish;
        } else if (argument == "subscribe" || argument == "sub") {
            options.mode = Mode::Subscribe;
        } else if (argument == "-h" || argument == "--help") {
            options.showHelp = true;
        } else if (argument == "-V" || argument == "--version") {
            options.showVersion = true;
        } else if (argument == "--host" || argument == "--port" || argument == "--topic" ||
                   argument == "--count" || argument == "--interval" ||
                   argument == "--client-id") {
            if (index + 1 >= argc) {
                error = argument + " requires a value";
                return options;
            }
            const std::string value = argv[++index];
            if (argument == "--host") {
                options.host = value;
            } else if (argument == "--port") {
                options.port = std::atoi(value.c_str());
            } else if (argument == "--topic") {
                options.topic = value;
            } else if (argument == "--count") {
                options.count = std::atoi(value.c_str());
            } else if (argument == "--interval") {
                options.intervalMs = std::atoi(value.c_str());
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

int MqttController::runPublisher(const Options& options) {
    model::MqttEndpoint endpoint;
    endpoint.host = options.host;
    endpoint.port = options.port;
    endpoint.clientId = options.clientId;

    std::string error;
    if (!client_.connect(endpoint, error)) {
        std::fprintf(stderr, "prts-mqtt: %s\n", error.c_str());
        return 2;
    }

    int sent = 0;
    while (options.count <= 0 || sent < options.count) {
        const std::string payload = telemetry_.sample();
        if (!client_.publish(options.topic, payload, error)) {
            std::fprintf(stderr, "prts-mqtt: publish failed: %s\n", error.c_str());
            client_.disconnect();
            return 3;
        }
        std::printf("%s %s\n", options.topic.c_str(), payload.c_str());
        std::fflush(stdout);
        ++sent;
        if (options.count > 0 && sent >= options.count) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(options.intervalMs));
    }

    client_.disconnect();
    return 0;
}

int MqttController::runSubscriber(const Options& options) {
    model::MqttEndpoint endpoint;
    endpoint.host = options.host;
    endpoint.port = options.port;
    endpoint.clientId = options.clientId + "-sub";

    std::string error;
    if (!client_.connect(endpoint, error)) {
        std::fprintf(stderr, "prts-mqtt: %s\n", error.c_str());
        return 2;
    }
    if (!client_.subscribe(options.topic, error)) {
        std::fprintf(stderr, "prts-mqtt: subscribe failed: %s\n", error.c_str());
        client_.disconnect();
        return 3;
    }

    int received = 0;
    while (options.count <= 0 || received < options.count) {
        model::MqttMessage message;
        if (client_.poll(500, message, error)) {
            std::printf("%s %s\n", message.topic.c_str(), message.payload.c_str());
            std::fflush(stdout);
            ++received;
            continue;
        }
        if (!error.empty()) {
            std::fprintf(stderr, "prts-mqtt: %s\n", error.c_str());
            client_.disconnect();
            return 4;
        }
    }

    client_.disconnect();
    return 0;
}

int MqttController::run(int argc, const char* const argv[]) {
    std::string error;
    const Options options = parse(argc, argv, error);
    if (!error.empty()) {
        std::fprintf(stderr, "prts-mqtt: %s\n", error.c_str());
        return 2;
    }
    if (options.showHelp) {
        printHelp();
        return 0;
    }
    if (options.showVersion) {
        std::printf("prts-mqtt %s\n", kMqttVersion);
        return 0;
    }

    logger_.info("mqtt " + std::string(options.mode == Mode::Publish ? "publish" : "subscribe") +
                 " " + options.host + ":" + std::to_string(options.port) + " topic=" + options.topic);

    return options.mode == Mode::Publish ? runPublisher(options) : runSubscriber(options);
}

}  // namespace prts::controller
