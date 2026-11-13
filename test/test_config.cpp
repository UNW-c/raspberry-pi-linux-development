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
    PRTS_CHECK_EQ(settings.charDelayMs, 10);
    PRTS_CHECK_EQ(settings.minColumnsFull, 78);
    PRTS_CHECK_EQ(settings.identity.systemName, std::string("PRTS"));
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
               << "accent = #11AFCF\n"
               << "warn = #FF8800\n"
               << "[identity]\n"
               << "operator = seven\n";
    }

    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/config-test.log");
    prts::service::ConfigService config(logger);
    PRTS_CHECK(config.loadFile(path));

    PRTS_CHECK_EQ(config.getInt("general.char_delay_ms", 99), 4);
    PRTS_CHECK_EQ(config.getBool("general.enabled", true), false);
    PRTS_CHECK_EQ(config.getInt("general.min_rows_full", 30), 34);
    PRTS_CHECK_EQ(config.getInt("general.check_delay_ms", 90), 90);

    const prts::model::Color accent = config.getColor("colors.accent", prts::model::Color());
    PRTS_CHECK(accent == prts::model::Color(0x11, 0xAF, 0xCF));
    PRTS_CHECK_EQ(config.getString("identity.operator", "nobody"), std::string("seven"));

    const prts::model::BootSettings settings = config.buildSettings();
    PRTS_CHECK(!settings.enabled);
    PRTS_CHECK_EQ(settings.charDelayMs, 4);
    PRTS_CHECK_EQ(settings.minRowsFull, 34);
    PRTS_CHECK(settings.palette.warn == prts::model::Color(0xFF, 0x88, 0x00));
}

PRTS_TEST(config_rejects_invalid_colour) {
    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/config-test.log");
    prts::service::ConfigService config(logger);
    const prts::model::Color fallback(1, 2, 3);
    PRTS_CHECK(config.getColor("colors.missing", fallback) == fallback);
}
