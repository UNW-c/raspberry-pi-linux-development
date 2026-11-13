#include "service/config_service.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include "utils/text.h"

namespace prts::service {
namespace {

std::string sectionedKey(const std::string& section, const std::string& key) {
    return section.empty() ? key : section + "." + key;
}

}  // namespace

ConfigService::ConfigService(utils::Logger& logger) : logger_(logger) {}

bool ConfigService::loadFile(const std::string& path) {
    std::ifstream stream(path);
    if (!stream.good()) {
        logger_.debug("config file not found: " + path);
        return false;
    }

    std::string section;
    std::string line;
    while (std::getline(stream, line)) {
        std::string cleaned = utils::Text::trim(line);
        if (cleaned.empty() || cleaned.front() == '#' || cleaned.front() == ';') {
            continue;
        }
        if (cleaned.front() == '[' && cleaned.back() == ']') {
            section = utils::Text::trim(cleaned.substr(1, cleaned.size() - 2));
            continue;
        }
        const std::size_t equals = cleaned.find('=');
        if (equals == std::string::npos) {
            logger_.warn("config: ignoring malformed line in " + path + ": " + cleaned);
            continue;
        }
        const std::string key = utils::Text::trim(cleaned.substr(0, equals));
        const std::string value = utils::Text::trim(cleaned.substr(equals + 1));
        if (key.empty()) {
            continue;
        }
        values_[sectionedKey(section, key)] = value;
    }

    logger_.info("config loaded: " + path + " (" + std::to_string(values_.size()) + " keys)");
    return true;
}

void ConfigService::loadDefaultLocations() {
    for (const std::string& candidate : defaultCandidatePaths()) {
        loadFile(candidate);
    }
}

std::string ConfigService::getString(std::string_view key, std::string_view fallback) const {
    const auto it = values_.find(std::string(key));
    if (it == values_.end() || it->second.empty()) {
        return std::string(fallback);
    }
    return it->second;
}

int ConfigService::getInt(std::string_view key, int fallback) const {
    const std::string raw = getString(key, std::string());
    if (raw.empty()) {
        return fallback;
    }
    char* end = nullptr;
    const long parsed = std::strtol(raw.c_str(), &end, 10);
    if (end == raw.c_str()) {
        logger_.warn("config: not an integer for " + std::string(key) + ": " + raw);
        return fallback;
    }
    return static_cast<int>(parsed);
}

bool ConfigService::getBool(std::string_view key, bool fallback) const {
    const std::string raw = utils::Text::lower(getString(key, std::string()));
    if (raw.empty()) {
        return fallback;
    }
    if (raw == "true" || raw == "1" || raw == "yes" || raw == "on") {
        return true;
    }
    if (raw == "false" || raw == "0" || raw == "no" || raw == "off") {
        return false;
    }
    logger_.warn("config: not a boolean for " + std::string(key) + ": " + raw);
    return fallback;
}

model::Color ConfigService::getColor(std::string_view key, const model::Color& fallback) const {
    const std::string raw = getString(key, std::string());
    if (raw.empty()) {
        return fallback;
    }
    model::Color parsed;
    if (!model::Color::fromHex(raw, parsed)) {
        logger_.warn("config: not a colour for " + std::string(key) + ": " + raw);
        return fallback;
    }
    return parsed;
}

model::BootSettings ConfigService::buildSettings() const {
    model::BootSettings settings;

    settings.enabled = getBool("general.enabled", settings.enabled);
    settings.charDelayMs = getInt("general.char_delay_ms", settings.charDelayMs);
    settings.frameDelayMs = getInt("general.frame_delay_ms", settings.frameDelayMs);
    settings.checkDelayMs = getInt("general.check_delay_ms", settings.checkDelayMs);
    settings.targetDurationMs = getInt("general.target_duration_ms", settings.targetDurationMs);
    settings.minColumnsFull = getInt("general.min_columns_full", settings.minColumnsFull);
    settings.minRowsFull = getInt("general.min_rows_full", settings.minRowsFull);
    settings.flashFrames = getInt("general.flash_frames", settings.flashFrames);

    settings.palette.background = getColor("colors.background", settings.palette.background);
    settings.palette.rule = getColor("colors.rule", settings.palette.rule);
    settings.palette.primary = getColor("colors.primary", settings.palette.primary);
    settings.palette.secondary = getColor("colors.secondary", settings.palette.secondary);
    settings.palette.muted = getColor("colors.muted", settings.palette.muted);
    settings.palette.subtle = getColor("colors.subtle", settings.palette.subtle);
    settings.palette.warn = getColor("colors.warn", settings.palette.warn);
    settings.palette.danger = getColor("colors.danger", settings.palette.danger);

    settings.identity.systemName = getString("identity.system_name", settings.identity.systemName);
    settings.identity.systemTag = getString("identity.system_tag", settings.identity.systemTag);
    settings.identity.operatorName =
        getString("identity.operator", settings.identity.operatorName);

    return settings;
}

std::vector<std::string> ConfigService::defaultCandidatePaths() {
    std::vector<std::string> candidates;

    // 1. Development tree: resolved relative to the executable so that running
    //    out of build/ still finds config/prts.conf.
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {
        candidates.push_back(std::string(home) + "/raspberry-pi-linux-development/config/prts.conf");
        candidates.push_back(std::string(home) + "/.config/prts/prts.conf");
    }
    // 2. System wide installs.
    candidates.push_back("/usr/local/share/prts/prts.conf");
    candidates.push_back("/usr/share/prts/prts.conf");

    // 3. Explicit override wins.
    if (const char* explicitPath = std::getenv("PRTS_CONFIG");
        explicitPath != nullptr && explicitPath[0] != '\0') {
        candidates.push_back(explicitPath);
    }
    return candidates;
}

}  // namespace prts::service
