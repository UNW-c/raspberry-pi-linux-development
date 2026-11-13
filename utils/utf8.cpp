#include "utils/utf8.h"

#include <cstddef>

namespace prts::utils {

std::vector<std::string> splitUtf8(std::string_view text) {
    std::vector<std::string> pieces;
    std::size_t index = 0;
    while (index < text.size()) {
        const auto lead = static_cast<unsigned char>(text[index]);
        std::size_t length = 1;
        if ((lead & 0xE0U) == 0xC0U) {
            length = 2;
        } else if ((lead & 0xF0U) == 0xE0U) {
            length = 3;
        } else if ((lead & 0xF8U) == 0xF0U) {
            length = 4;
        }
        if (index + length > text.size()) {
            length = 1;
        }
        pieces.emplace_back(text.substr(index, length));
        index += length;
    }
    return pieces;
}

}  // namespace prts::utils
