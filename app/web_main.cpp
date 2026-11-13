#include <cstdlib>

#include "controller/web_controller.h"
#include "driver/tcp_mqtt_client.h"
#include "service/camera_web_service.h"
#include "utils/logger.h"

int main(int argc, char** argv) {
    const char* levelEnvironment = std::getenv("PRTS_LOG_LEVEL");
    const prts::utils::LogLevel level =
        prts::utils::Logger::levelFromName(levelEnvironment != nullptr ? levelEnvironment : "warn");
    prts::utils::Logger logger(prts::utils::Logger::defaultLogPath(), level);

    prts::driver::TcpMqttClient client;
    prts::service::CameraWebService web(client, logger);
    prts::controller::WebController controller(web, logger);
    return controller.run(argc, argv);
}
