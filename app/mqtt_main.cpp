#include <cstdlib>

#include "controller/mqtt_controller.h"
#include "driver/linux_system_probe.h"
#include "driver/steady_clock_driver.h"
#include "driver/tcp_mqtt_client.h"
#include "service/telemetry_service.h"
#include "utils/logger.h"

int main(int argc, char** argv) {
    const char* levelEnvironment = std::getenv("PRTS_LOG_LEVEL");
    const prts::utils::LogLevel level =
        prts::utils::Logger::levelFromName(levelEnvironment != nullptr ? levelEnvironment : "warn");
    prts::utils::Logger logger(prts::utils::Logger::defaultLogPath(), level);

    prts::driver::LinuxSystemProbe probe;
    prts::driver::SteadyClockDriver clock;
    prts::driver::TcpMqttClient client;
    prts::service::TelemetryService telemetry(probe, clock);

    prts::controller::MqttController controller(client, telemetry, logger);
    return controller.run(argc, argv);
}
