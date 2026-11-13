#pragma once

#include <string>

#include "model/boot_settings.h"
#include "service/animation_service.h"
#include "service/boot_sequence_service.h"
#include "service/config_service.h"
#include "utils/logger.h"

namespace prts::controller {

inline constexpr const char* kVersion = "0.1.0";

// Entry point of the application layer. Owns argument parsing and decides
// whether the cinematic sequence runs or a plain diagnostic report is printed.
class BootController {
public:
    struct Options {
        bool animate{true};
        bool checkOnly{false};
        bool force{false};
        bool showHelp{false};
        bool showVersion{false};
        std::string configPath;
    };

    BootController(service::ConfigService& config, service::AnimationService& animation,
                   service::BootSequenceService& boot, const model::BootSettings& settings,
                   utils::Logger& logger);

    int run(int argc, const char* const argv[]);

    static Options parse(int argc, const char* const argv[], std::string& error);
    static void printHelp();

private:
    bool animationSuppressedByEnvironment() const;

    service::ConfigService& config_;
    service::AnimationService& animation_;
    service::BootSequenceService& boot_;
    const model::BootSettings& settings_;
    utils::Logger& logger_;
};

}  // namespace prts::controller
