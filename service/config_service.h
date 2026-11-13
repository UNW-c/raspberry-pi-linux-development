#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "model/boot_settings.h"
#include "utils/logger.h"

namespace prts::service {

// Loads the layered configuration (installed defaults, user override, explicit
// file) into a flat key/value map and materialises typed settings from it.
class ConfigService {
public:
    explicit ConfigService(utils::Logger& logger);

    // Returns false when the file is missing or unreadable; the rest of the
    // configuration stays untouched so that defaults keep working.
    bool loadFile(const std::string& path);

    // Loads every candidate path that exists, lowest priority first.
    void loadDefaultLocations();

    std::string getString(std::string_view key, std::string_view fallback) const;
    int getInt(std::string_view key, int fallback) const;
    bool getBool(std::string_view key, bool fallback) const;
    model::Color getColor(std::string_view key, const model::Color& fallback) const;

    // Materialise the strongly typed settings used by the application layer.
    model::BootSettings buildSettings() const;

    static std::vector<std::string> defaultCandidatePaths();
    std::size_t size() const { return values_.size(); }

private:
    utils::Logger& logger_;
    std::map<std::string, std::string> values_;
};

}  // namespace prts::service
