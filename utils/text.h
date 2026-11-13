#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace prts::utils {

// UTF-8 aware string helpers. The boot sequence prints CJK text, so byte based
// padding would break alignment; width is therefore counted in terminal cells.
class Text {
public:
    static std::size_t byteLength(std::string_view text);

    // Number of terminal cells occupied by the text (wide CJK = 2, combining = 0).
    static std::size_t displayWidth(std::string_view text);

    static std::string padRight(std::string_view text, std::size_t width);
    static std::string center(std::string_view text, std::size_t width);
    static std::string repeat(std::string_view unit, std::size_t count);

    // Cuts the text to fit the given cell width, appending the ellipsis when it
    // had to be shortened.
    static std::string truncate(std::string_view text, std::size_t width, std::string_view ellipsis = "..");
    // Truncate + pad, so every call returns exactly `width` cells.
    static std::string fit(std::string_view text, std::size_t width);

    static std::string upper(std::string_view text);
    static std::string lower(std::string_view text);
    static std::string trim(std::string_view text);

    static std::vector<std::string> split(std::string_view text, char separator);
    static std::string join(const std::vector<std::string>& parts, std::string_view separator);

    static bool startsWith(std::string_view text, std::string_view prefix);
    static bool equalsIgnoreCase(std::string_view left, std::string_view right);

    // "1.2 GiB" / "905 MiB" style helpers, used by the self-check rows.
    static std::string formatKib(std::uint64_t kibibytes);
    static std::string formatBytes(std::uint64_t bytes);
    static std::string formatDuration(double seconds);
    static std::string formatFloat(double value, int decimals);
};

}  // namespace prts::utils
