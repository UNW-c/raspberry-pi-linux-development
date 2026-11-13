#include "utils/text.h"

#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <sstream>

namespace prts::utils {
namespace {

// Decode one UTF-8 code point. Invalid bytes advance by one and yield U+FFFD.
std::uint32_t decodeUtf8(std::string_view text, std::size_t& index) {
    const auto byte = static_cast<unsigned char>(text[index]);
    auto continuation = [&text](std::size_t offset) -> bool {
        return offset < text.size() &&
               (static_cast<unsigned char>(text[offset]) & 0xC0U) == 0x80U;
    };

    if (byte < 0x80U) {
        ++index;
        return byte;
    }
    if ((byte & 0xE0U) == 0xC0U && continuation(index + 1)) {
        const std::uint32_t cp = ((byte & 0x1FU) << 6) |
                                 (static_cast<unsigned char>(text[index + 1]) & 0x3FU);
        index += 2;
        return cp;
    }
    if ((byte & 0xF0U) == 0xE0U && continuation(index + 1) && continuation(index + 2)) {
        const std::uint32_t cp = ((byte & 0x0FU) << 12) |
                                 ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 6) |
                                 (static_cast<unsigned char>(text[index + 2]) & 0x3FU);
        index += 3;
        return cp;
    }
    if ((byte & 0xF8U) == 0xF0U && continuation(index + 1) && continuation(index + 2) &&
        continuation(index + 3)) {
        const std::uint32_t cp = ((byte & 0x07U) << 18) |
                                 ((static_cast<unsigned char>(text[index + 1]) & 0x3FU) << 12) |
                                 ((static_cast<unsigned char>(text[index + 2]) & 0x3FU) << 6) |
                                 (static_cast<unsigned char>(text[index + 3]) & 0x3FU);
        index += 4;
        return cp;
    }
    ++index;
    return 0xFFFDU;
}

bool isCombining(std::uint32_t cp) {
    return (cp >= 0x0300U && cp <= 0x036FU) || (cp >= 0x1AB0U && cp <= 0x1AFFU) ||
           (cp >= 0x20D0U && cp <= 0x20FFU) || (cp >= 0xFE00U && cp <= 0xFE0FU);
}

bool isWide(std::uint32_t cp) {
    return (cp >= 0x1100U && cp <= 0x115FU) || (cp >= 0x2E80U && cp <= 0x303EU) ||
           (cp >= 0x3041U && cp <= 0x33FFU) || (cp >= 0x3400U && cp <= 0x4DBFU) ||
           (cp >= 0x4E00U && cp <= 0x9FFFU) || (cp >= 0xA000U && cp <= 0xA4CFU) ||
           (cp >= 0xA960U && cp <= 0xA97FU) || (cp >= 0xAC00U && cp <= 0xD7A3U) ||
           (cp >= 0xF900U && cp <= 0xFAFFU) || (cp >= 0xFE10U && cp <= 0xFE19U) ||
           (cp >= 0xFE30U && cp <= 0xFE6FU) || (cp >= 0xFF00U && cp <= 0xFF60U) ||
           (cp >= 0xFFE0U && cp <= 0xFFE6U) || (cp >= 0x1F300U && cp <= 0x1F64FU) ||
           (cp >= 0x1F900U && cp <= 0x1F9FFU) || (cp >= 0x20000U && cp <= 0x3FFFD);
}

int codePointWidth(std::uint32_t cp) {
    if (cp == 0U || cp == 0xFFFDU) {
        return 1;
    }
    if (cp < 0x20U || (cp >= 0x7FU && cp <= 0x9FU)) {
        return 0;
    }
    if (isCombining(cp)) {
        return 0;
    }
    return isWide(cp) ? 2 : 1;
}

}  // namespace

std::size_t Text::byteLength(std::string_view text) { return text.size(); }

std::size_t Text::displayWidth(std::string_view text) {
    std::size_t index = 0;
    std::size_t width = 0;
    while (index < text.size()) {
        width += static_cast<std::size_t>(codePointWidth(decodeUtf8(text, index)));
    }
    return width;
}

std::string Text::padRight(std::string_view text, std::size_t width) {
    std::string result(text);
    const std::size_t current = displayWidth(text);
    if (current < width) {
        result.append(width - current, ' ');
    }
    return result;
}

std::string Text::center(std::string_view text, std::size_t width) {
    const std::size_t current = displayWidth(text);
    if (current >= width) {
        return std::string(text);
    }
    const std::size_t left = (width - current) / 2;
    return std::string(left, ' ') + std::string(text) + std::string(width - current - left, ' ');
}

std::string Text::truncate(std::string_view text, std::size_t width, std::string_view ellipsis) {
    if (displayWidth(text) <= width) {
        return std::string(text);
    }
    const std::size_t ellipsisWidth = displayWidth(ellipsis);
    if (width <= ellipsisWidth) {
        return std::string(ellipsis.substr(0, width));
    }
    const std::size_t budget = width - ellipsisWidth;
    std::size_t index = 0;
    std::size_t used = 0;
    while (index < text.size()) {
        const std::size_t before = index;
        const int widthOfChar = codePointWidth(decodeUtf8(text, index));
        if (used + static_cast<std::size_t>(std::max(widthOfChar, 0)) > budget) {
            index = before;
            break;
        }
        used += static_cast<std::size_t>(std::max(widthOfChar, 0));
    }
    return std::string(text.substr(0, index)) + std::string(ellipsis);
}

std::string Text::fit(std::string_view text, std::size_t width) {
    return padRight(truncate(text, width), width);
}

std::string Text::repeat(std::string_view unit, std::size_t count) {
    std::string result;
    result.reserve(unit.size() * count);
    for (std::size_t i = 0; i < count; ++i) {
        result.append(unit);
    }
    return result;
}

std::string Text::upper(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'a' && character <= 'z') {
            character = static_cast<char>(character - 'a' + 'A');
        }
    }
    return result;
}

std::string Text::lower(std::string_view text) {
    std::string result(text);
    for (char& character : result) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return result;
}

std::string Text::trim(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    auto isSpace = [](char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    while (begin < end && isSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && isSpace(text[end - 1])) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

std::vector<std::string> Text::split(std::string_view text, char separator) {
    std::vector<std::string> parts;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t position = text.find(separator, start);
        if (position == std::string_view::npos) {
            parts.emplace_back(text.substr(start));
            break;
        }
        parts.emplace_back(text.substr(start, position - start));
        start = position + 1;
    }
    return parts;
}

std::string Text::join(const std::vector<std::string>& parts, std::string_view separator) {
    std::string result;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i != 0) {
            result.append(separator);
        }
        result.append(parts[i]);
    }
    return result;
}

bool Text::startsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool Text::equalsIgnoreCase(std::string_view left, std::string_view right) {
    return upper(left) == upper(right);
}

std::string Text::formatFloat(double value, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, value);
    return std::string(buffer);
}

std::string Text::formatKib(std::uint64_t kibibytes) {
    const double value = static_cast<double>(kibibytes);
    char buffer[64];
    if (kibibytes >= 1024ULL * 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GiB", value / (1024.0 * 1024.0));
    } else if (kibibytes >= 1024ULL) {
        std::snprintf(buffer, sizeof(buffer), "%.0f MiB", value / 1024.0);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f KiB", value);
    }
    return std::string(buffer);
}

std::string Text::formatBytes(std::uint64_t bytes) {
    return formatKib(bytes / 1024ULL);
}

std::string Text::formatDuration(double seconds) {
    if (seconds < 0.0) {
        seconds = 0.0;
    }
    const auto total = static_cast<std::uint64_t>(seconds);
    const std::uint64_t days = total / 86400ULL;
    const std::uint64_t hours = (total % 86400ULL) / 3600ULL;
    const std::uint64_t minutes = (total % 3600ULL) / 60ULL;
    char buffer[64];
    if (days > 0) {
        std::snprintf(buffer, sizeof(buffer), "%llud %02llu:%02llu",
                      static_cast<unsigned long long>(days),
                      static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02llu:%02llu",
                      static_cast<unsigned long long>(hours),
                      static_cast<unsigned long long>(minutes));
    }
    return std::string(buffer);
}

}  // namespace prts::utils
