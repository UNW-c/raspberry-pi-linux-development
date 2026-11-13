#include "service/logo_art.h"

#include <algorithm>

#include "utils/text.h"

namespace prts::service {

const std::vector<std::string>& LogoArt::emblem() {
    static const std::vector<std::string> rows = {
        "     \u2588\u2588\u2588     ",
        "   \u2588\u2588\u2588\u2588\u2588\u2588\u2588   ",
        " \u2588\u2588\u2588     \u2588\u2588\u2588 ",
        "\u2588\u2588\u2588   \u25c6   \u2588\u2588\u2588",
        " \u2588\u2588\u2588     \u2588\u2588\u2588 ",
        "   \u2588\u2588\u2588\u2588\u2588\u2588\u2588   ",
        "     \u2588\u2588\u2588     ",
    };
    return rows;
}

const std::vector<std::string>& LogoArt::wordmark() {
    static const std::vector<std::string> rows = {
        "\u2588\u2588\u2588\u2588 \u2588\u2588\u2588\u2588 \u2588\u2588\u2588\u2588\u2588\u2588 \u2588\u2588\u2588\u2588",
        "\u2588  \u2588 \u2588  \u2588   \u2588\u2588   \u2588   ",
        "\u2588\u2588\u2588\u2588 \u2588\u2588\u2588\u2588   \u2588\u2588   \u2588\u2588\u2588\u2588",
        "\u2588    \u2588 \u2588    \u2588\u2588      \u2588",
        "\u2588    \u2588  \u2588   \u2588\u2588   \u2588\u2588\u2588\u2588",
    };
    return rows;
}

std::string LogoArt::compactMark(const std::string& systemName) {
    return "\u25c6 " + systemName + " \u25c6";
}

int LogoArt::gridWidth(const std::vector<std::string>& rows) {
    int width = 0;
    for (const std::string& row : rows) {
        width = std::max(width, static_cast<int>(utils::Text::displayWidth(row)));
    }
    return width;
}

}  // namespace prts::service
