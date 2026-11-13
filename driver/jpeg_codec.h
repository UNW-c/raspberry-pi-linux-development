#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace prts::driver {

// Decodes a JPEG, rescales it with libjpeg's DCT scaling (1/2, 1/4, 1/8) and
// re-encodes it at the requested quality. The camera only offers 640x480 and
// 1280x720, so this is how the frame is made small enough for a slow link.
//
// On success `output` holds the new JPEG and width/height describe it.
bool scaleJpeg(const std::vector<std::uint8_t>& input, int scaleDenom, int quality,
               std::vector<std::uint8_t>& output, int& width, int& height, std::string& error);

// Reads the frame size out of the JPEG SOF marker without decoding pixels.
bool jpegSize(const std::vector<std::uint8_t>& data, int& width, int& height);

}  // namespace prts::driver
