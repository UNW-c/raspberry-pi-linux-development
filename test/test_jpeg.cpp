#include <cstdint>
#include <string>
#include <vector>

#include "driver/jpeg_codec.h"
#include "test/test_framework.h"

PRTS_TEST(jpeg_rejects_non_jpeg_input) {
    std::vector<std::uint8_t> input = {0x00, 0x01, 0x02, 0x03};
    std::vector<std::uint8_t> output;
    int width = 0;
    int height = 0;
    std::string error;
    PRTS_CHECK(!prts::driver::scaleJpeg(input, 2, 70, output, width, height, error));
    PRTS_CHECK(!error.empty());
    PRTS_CHECK(output.empty());
}

PRTS_TEST(jpeg_rejects_unknown_scale_factor) {
    std::vector<std::uint8_t> input = {0xFF, 0xD8, 0x00, 0x00};
    std::vector<std::uint8_t> output;
    int width = 0;
    int height = 0;
    std::string error;
    PRTS_CHECK(!prts::driver::scaleJpeg(input, 3, 70, output, width, height, error));
    PRTS_CHECK_EQ(error, std::string("scale must be 1, 2, 4 or 8"));
}

PRTS_TEST(jpeg_size_parsing) {
    int width = 0;
    int height = 0;
    PRTS_CHECK(!prts::driver::jpegSize({}, width, height));
    PRTS_CHECK(!prts::driver::jpegSize({0x00, 0x00}, width, height));

    // Minimal SOI + SOF0 header describing a 320x240 frame.
    std::vector<std::uint8_t> header = {0xFF, 0xD8, 0xFF, 0xC0, 0x00, 0x11, 0x08,
                                        0x00, 0xF0, 0x01, 0x40, 0x03, 0x01, 0x22,
                                        0x00, 0x02, 0x11, 0x01, 0x03, 0x11, 0x01};
    PRTS_CHECK(prts::driver::jpegSize(header, width, height));
    PRTS_CHECK_EQ(height, 240);
    PRTS_CHECK_EQ(width, 320);
}
