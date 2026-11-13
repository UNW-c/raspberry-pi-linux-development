#include "controller/camera_controller.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace prts::controller {
namespace {

const char* environmentOrNull(const char* name) {
    const char* value = std::getenv(name);
    return (value == nullptr || value[0] == '\0') ? nullptr : value;
}

}  // namespace

CameraController::CameraController(driver::IMqttClient& client, service::CameraService& camera,
                                   driver::IClock& clock, utils::Logger& logger)
    : client_(client), camera_(camera), clock_(clock), logger_(logger) {}

void CameraController::printHelp() {
    std::printf(
        "prts-camera %s - MJPEG frames from a V4L2 camera to MQTT\n"
        "\n"
        "Usage: prts-camera [options]\n"
        "\n"
        "Options:\n"
        "  --host HOST        broker address (default 127.0.0.1)\n"
        "  --port PORT        broker port (default 1883)\n"
        "  --device PATH      V4L2 device (default /dev/video0)\n"
        "  --width N          capture width (default 640)\n"
        "  --height N         capture height (default 480)\n"
        "  --fps N            publish rate (default 10)\n"
        "  --frame-topic T    topic carrying the JPEG bytes (default prts/camera/frame)\n"
        "  --meta-topic T     topic carrying frame metadata (default prts/camera/meta)\n"
        "  --meta-every N     publish metadata every N frames (default 30)\n"
        "  --count N          stop after N frames (default 0 = until interrupted)\n"
        "  --client-id ID     MQTT client id (default prts-camera)\n"
        "  -h, --help         show this help\n"
        "  -V, --version      print the version\n"
        "\n"
        "Environment: PRTS_MQTT_HOST, PRTS_MQTT_PORT\n",
        kCameraVersion);
}

CameraController::Options CameraController::parse(int argc, const char* const argv[],
                                                  std::string& error) {
    Options options;
    error.clear();

    if (const char* host = environmentOrNull("PRTS_MQTT_HOST"); host != nullptr) {
        options.host = host;
    }
    if (const char* port = environmentOrNull("PRTS_MQTT_PORT"); port != nullptr) {
        options.port = std::atoi(port);
    }

    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        if (argument == "-h" || argument == "--help") {
            options.showHelp = true;
        } else if (argument == "-V" || argument == "--version") {
            options.showVersion = true;
        } else if (argument == "--host" || argument == "--port" || argument == "--device" ||
                   argument == "--width" || argument == "--height" || argument == "--fps" ||
                   argument == "--frame-topic" || argument == "--meta-topic" ||
                   argument == "--meta-every" || argument == "--count" ||
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
            } else if (argument == "--device") {
                options.device = value;
            } else if (argument == "--width") {
                options.width = std::atoi(value.c_str());
            } else if (argument == "--height") {
                options.height = std::atoi(value.c_str());
            } else if (argument == "--fps") {
                options.fps = std::atoi(value.c_str());
            } else if (argument == "--frame-topic") {
                options.frameTopic = value;
            } else if (argument == "--meta-topic") {
                options.metaTopic = value;
            } else if (argument == "--meta-every") {
                options.metaEvery = std::atoi(value.c_str());
            } else if (argument == "--count") {
                options.maxFrames = std::atoi(value.c_str());
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

int CameraController::stream(const Options& options) {
    model::MqttEndpoint endpoint;
    endpoint.host = options.host;
    endpoint.port = options.port;
    endpoint.clientId = options.clientId;
    endpoint.keepAliveSeconds = 60;

    std::string error;
    if (!client_.connect(endpoint, error)) {
        std::fprintf(stderr, "prts-camera: mqtt: %s\n", error.c_str());
        return 3;
    }

    model::CameraConfig config;
    config.device = options.device;
    config.width = options.width;
    config.height = options.height;
    config.fps = options.fps;
    if (!camera_.open(config, error)) {
        std::fprintf(stderr, "prts-camera: %s\n", error.c_str());
        client_.disconnect();
        return 4;
    }

    const int intervalMs = options.fps > 0 ? std::max(1, 1000 / options.fps) : 100;
    const int metaEvery = std::max(1, options.metaEvery);
    int captured = 0;
    std::uint64_t publishedBytes = 0;

    while (options.maxFrames <= 0 || captured < options.maxFrames) {
        const std::int64_t frameStart = clock_.steadyMillis();

        model::CameraFrame frame;
        if (!camera_.grab(frame, 2000, error)) {
            if (error == "timeout") {
                continue;
            }
            std::fprintf(stderr, "prts-camera: capture failed: %s\n", error.c_str());
            camera_.close();
            client_.disconnect();
            return 5;
        }

        const std::string payload(reinterpret_cast<const char*>(frame.data.data()), frame.data.size());
        if (!client_.publish(options.frameTopic, payload, error)) {
            std::fprintf(stderr, "prts-camera: publish failed: %s\n", error.c_str());
            camera_.close();
            client_.disconnect();
            return 6;
        }
        publishedBytes += frame.data.size();
        ++captured;

        if (captured % metaEvery == 0) {
            const std::string meta =
                service::CameraService::metaJson(frame, clock_.wallClock("%Y-%m-%dT%H:%M:%S"));
            if (!client_.publish(options.metaTopic, meta, error)) {
                std::fprintf(stderr, "prts-camera: publish meta failed: %s\n", error.c_str());
            }
            std::printf("frame %u  %zu B  %dx%d  %s\n", frame.sequence, frame.data.size(),
                        frame.width, frame.height, meta.c_str());
            std::fflush(stdout);
        }

        const std::int64_t elapsed = clock_.steadyMillis() - frameStart;
        if (elapsed < intervalMs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(intervalMs - elapsed));
        }
    }

    camera_.close();
    client_.disconnect();
    logger_.info("camera stream finished: " + std::to_string(captured) + " frames, " +
                 std::to_string(publishedBytes) + " bytes");
    return 0;
}

int CameraController::run(int argc, const char* const argv[]) {
    std::string error;
    const Options options = parse(argc, argv, error);
    if (!error.empty()) {
        std::fprintf(stderr, "prts-camera: %s\n", error.c_str());
        return 2;
    }
    if (options.showHelp) {
        printHelp();
        return 0;
    }
    if (options.showVersion) {
        std::printf("prts-camera %s\n", kCameraVersion);
        return 0;
    }
    return stream(options);
}

}  // namespace prts::controller
