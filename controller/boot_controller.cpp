#include "controller/boot_controller.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace prts::controller {
namespace {

bool environmentFlagSet(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::string(value) != "0";
}

}  // namespace

BootController::BootController(service::ConfigService& config, service::AnimationService& animation,
                               service::BootSequenceService& boot,
                               const model::BootSettings& settings, utils::Logger& logger)
    : config_(config), animation_(animation), boot_(boot), settings_(settings), logger_(logger) {}

BootController::Options BootController::parse(int argc, const char* const argv[],
                                              std::string& error) {
    Options options;
    error.clear();
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index] != nullptr ? argv[index] : "";
        if (argument == "-h" || argument == "--help") {
            options.showHelp = true;
        } else if (argument == "-V" || argument == "--version") {
            options.showVersion = true;
        } else if (argument == "--check" || argument == "--plain") {
            options.checkOnly = true;
        } else if (argument == "--force") {
            options.force = true;
        } else if (argument == "--config") {
            if (index + 1 >= argc) {
                error = "--config requires a path";
                return options;
            }
            options.configPath = argv[++index];
        } else {
            error = "unknown argument: " + argument;
            return options;
        }
    }
    return options;
}

bool BootController::animationSuppressedByEnvironment() const {
    return environmentFlagSet("PRTS_SKIP_BOOT") || environmentFlagSet("PRTS_NO_ANIMATION");
}

void BootController::printHelp() {
    std::printf(
        "prts-boot %s - PRTS terminal boot sequence\n"
        "\n"
        "Usage: prts-boot [options]\n"
        "\n"
        "Options:\n"
        "  -h, --help        show this help\n"
        "  -V, --version     print the version\n"
        "      --check       run the self-check and print a plain report\n"
        "      --force       run even if the config or the environment disables it\n"
        "      --config PATH load an extra configuration file\n"
        "\n"
        "Environment:\n"
        "  PRTS_SKIP_BOOT=1   skip the sequence (used by the login hook)\n"
        "  PRTS_LOG_LEVEL     debug | info | warn | error\n"
        "  PRTS_LOG_FILE      override the log destination\n"
        "  NO_COLOR=1         disable colour output\n",
        kVersion);
}

int BootController::run(int argc, const char* const argv[]) {
    std::string error;
    const Options options = parse(argc, argv, error);
    if (!error.empty()) {
        std::fprintf(stderr, "prts-boot: %s\n", error.c_str());
        return 2;
    }
    if (options.showHelp) {
        printHelp();
        return 0;
    }
    if (options.showVersion) {
        std::printf("prts-boot %s\n", kVersion);
        return 0;
    }

    if (!options.force) {
        if (!settings_.enabled) {
            logger_.debug("boot sequence disabled by configuration");
            return 0;
        }
        if (animationSuppressedByEnvironment()) {
            logger_.debug("boot sequence disabled by environment");
            return 0;
        }
    }

    const bool animate = options.animate && !options.checkOnly && animation_.isInteractive();
    logger_.info(std::string("running boot sequence, animate=") + (animate ? "true" : "false") +
                 ", config keys=" + std::to_string(config_.size()));

    const service::BootSequenceService::Outcome outcome = boot_.run(animate);
    if (outcome.failCount > 0) {
        return 1;
    }
    return 0;
}

}  // namespace prts::controller
