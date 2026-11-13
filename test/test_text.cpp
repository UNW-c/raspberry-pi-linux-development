#include <string>

#include "test/test_framework.h"
#include "utils/text.h"

using prts::utils::Text;

PRTS_TEST(text_display_width_counts_cjk_as_two_cells) {
    PRTS_CHECK_EQ(Text::displayWidth("PRTS"), std::size_t{4});
    PRTS_CHECK_EQ(Text::displayWidth("原生罗德岛"), std::size_t{10});
    PRTS_CHECK_EQ(Text::displayWidth("MEM 17%"), std::size_t{7});
    PRTS_CHECK_EQ(Text::displayWidth(std::string()), std::size_t{0});
}

PRTS_TEST(text_padding_and_fitting) {
    PRTS_CHECK_EQ(Text::padRight("OK", 5), std::string("OK   "));
    PRTS_CHECK_EQ(Text::center("AB", 6), std::string("  AB  "));
    PRTS_CHECK_EQ(Text::fit("abcdef", std::size_t{3}), std::string("a.."));
    PRTS_CHECK_EQ(Text::fit("ok", 4), std::string("ok  "));
    PRTS_CHECK_EQ(Text::truncate("abcdef", std::size_t{6}), std::string("abcdef"));
}

PRTS_TEST(text_format_helpers) {
    PRTS_CHECK_EQ(Text::formatKib(766000), std::string("748 MiB"));
    PRTS_CHECK_EQ(Text::formatKib(512), std::string("512 KiB"));
    PRTS_CHECK_EQ(Text::formatDuration(93784.0), std::string("1d 02:03"));
    PRTS_CHECK_EQ(Text::formatDuration(90.0), std::string("00:01"));
    PRTS_CHECK_EQ(Text::formatFloat(38.64, 1), std::string("38.6"));
}

PRTS_TEST(text_split_and_join) {
    const auto parts = Text::split("a=b=c", '=');
    PRTS_CHECK_EQ(parts.size(), std::size_t{3});
    PRTS_CHECK_EQ(parts[1], std::string("b"));
    PRTS_CHECK_EQ(Text::join(parts, "-"), std::string("a-b-c"));
}
