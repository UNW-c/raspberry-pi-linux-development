#include <cstdio>
#include <cstdlib>
#include <string>

#include "controller/boot_controller.h"
#include "driver/ansi_terminal.h"
#include "driver/linux_system_probe.h"
#include "driver/steady_clock_driver.h"
#include "model/boot_settings.h"
#include "service/animation_service.h"
#include "service/boot_sequence_service.h"
#include "service/config_service.h"
#include "service/selfcheck_service.h"
#include "service/session_state_service.h"
#include "utils/logger.h"

namespace {

// The composition root resolves --config itself: settings must exist before
// the services that capture a reference to them are constructed.
std::string findConfigArgument(int argc, char** argv) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == "--config") {
            return std::string(argv[index + 1]);
        }
    }
    return std::string();
}

}  // namespace

int main(int argc, char** argv) {
    const char* levelEnvironment = std::getenv("PRTS_LOG_LEVEL");
    const prts::utils::LogLevel level = prts::utils::Logger::levelFromName(
        levelEnvironment != nullptr ? levelEnvironment : "warn");
    prts::utils::Logger logger(prts::utils::Logger::defaultLogPath(), level);

    prts::service::ConfigService config(logger);
    config.loadDefaultLocations();
    const std::string explicitConfig = findConfigArgument(argc, argv);
    if (!explicitConfig.empty()) {
        config.loadFile(explicitConfig);
    }

    const prts::model::BootSettings settings = config.buildSettings();

    prts::driver::AnsiTerminal terminal;
    prts::driver::SteadyClockDriver clock;
    prts::driver::LinuxSystemProbe probe;

    prts::service::AnimationService animation(terminal, clock, settings, logger);
    prts::service::SelfcheckService selfcheck(probe, clock, logger);
    prts::service::SessionStateService session(prts::service::SessionStateService::defaultStatePath());
    prts::service::BootSequenceService boot(animation, selfcheck, session, settings, logger);

    prts::controller::BootController controller(config, animation, boot, settings, logger);
    return controller.run(argc, argv);
}
