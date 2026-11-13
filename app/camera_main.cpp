#include <cstdlib>

#include "controller/camera_controller.h"
#include "driver/steady_clock_driver.h"
#include "driver/tcp_mqtt_client.h"
#include "driver/v4l2_camera.h"
#include "service/camera_service.h"
#include "utils/logger.h"

int main(int argc, char** argv) {
    const char* levelEnvironment = std::getenv("PRTS_LOG_LEVEL");
    const prts::utils::LogLevel level =
        prts::utils::Logger::levelFromName(levelEnvironment != nullptr ? levelEnvironment : "warn");
    prts::utils::Logger logger(prts::utils::Logger::defaultLogPath(), level);

    prts::driver::SteadyClockDriver clock;
    prts::driver::V4l2Camera source;
    prts::driver::TcpMqttClient client;
    prts::service::CameraService camera(source, clock);

    prts::controller::CameraController controller(client, camera, clock, logger);
    return controller.run(argc, argv);
}
