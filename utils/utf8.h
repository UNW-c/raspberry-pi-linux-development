#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prts::utils {

// Splits UTF-8 text into individual code points, preserving the original byte
// sequences so that they can be re-emitted one at a time.
std::vector<std::string> splitUtf8(std::string_view text);

}  // namespace prts::utils
