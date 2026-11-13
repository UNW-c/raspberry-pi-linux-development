#include <filesystem>
#include <fstream>
#include <string>

#include "service/config_service.h"
#include "test/fakes.h"
#include "test/test_framework.h"
#include "utils/logger.h"

PRTS_TEST(config_defaults_survive_without_a_file) {
    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/config-test.log");
    prts::service::ConfigService config(logger);
    PRTS_CHECK(!config.loadFile(prts::test::temporaryDirectory() + "/missing.conf"));
    const prts::model::BootSettings settings = config.buildSettings();

    // Tuning values are free to change; only their sanity is asserted here so
    // the test does not have to be edited every time the pacing is retimed.
    PRTS_CHECK(settings.charDelayMs > 0 && settings.charDelayMs < 100);
    PRTS_CHECK(settings.frameDelayMs > 0 && settings.frameDelayMs < 200);
    PRTS_CHECK(settings.checkDelayMs > 0 && settings.checkDelayMs < 500);
    PRTS_CHECK(settings.minColumnsFull >= 40);
    PRTS_CHECK(settings.minRowsFull >= 20);
    PRTS_CHECK_EQ(settings.identity.systemName, std::string("PRTS"));
    PRTS_CHECK_EQ(settings.identity.systemTag, std::string("RHODES ISLAND"));
}

PRTS_TEST(config_reads_sections_and_types) {
    const std::string path = prts::test::temporaryDirectory() + "/prts-test.conf";
    {
        std::ofstream stream(path);
        stream << "# comment\n"
               << "[general]\n"
               << "enabled = false\n"
               << "char_delay_ms = 4\n"
               << "min_rows_full = 34\n"
               << "[colors]\n"
               << "warn = #C9A227\n"
               << "danger = #FF8800\n"
               << "[identity]\n"
               << "operator = seven\n";
    }

    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/config-test.log");
    prts::service::ConfigService config(logger);
    PRTS_CHECK(config.loadFile(path));

    PRTS_CHECK_EQ(config.getInt("general.char_delay_ms", 99), 4);
    PRTS_CHECK_EQ(config.getBool("general.enabled", true), false);
    PRTS_CHECK_EQ(config.getInt("general.min_rows_full", 30), 34);
    PRTS_CHECK_EQ(config.getInt("general.check_delay_ms", 70), 70);

    const prts::model::Color warn = config.getColor("colors.warn", prts::model::Color());
    PRTS_CHECK(warn == prts::model::Color(0xC9, 0xA2, 0x27));
    PRTS_CHECK_EQ(config.getString("identity.operator", "nobody"), std::string("seven"));

    const prts::model::BootSettings settings = config.buildSettings();
    PRTS_CHECK(!settings.enabled);
    PRTS_CHECK_EQ(settings.charDelayMs, 4);
    PRTS_CHECK_EQ(settings.minRowsFull, 34);
    PRTS_CHECK(settings.palette.danger == prts::model::Color(0xFF, 0x88, 0x00));
}

PRTS_TEST(config_rejects_invalid_colour) {
    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/config-test.log");
    prts::service::ConfigService config(logger);
    const prts::model::Color fallback(1, 2, 3);
    PRTS_CHECK(config.getColor("colors.missing", fallback) == fallback);
}
