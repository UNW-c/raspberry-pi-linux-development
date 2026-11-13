#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>

namespace prts::model {

// RGB colour value used by the rendering layer. Colour theory (gradients,
// mixing) lives here so that drivers only ever deal with plain numbers.
class Color {
public:
    constexpr Color() = default;
    constexpr Color(std::uint8_t red, std::uint8_t green, std::uint8_t blue)
        : red_(red), green_(green), blue_(blue) {}

    // Accepts "#RRGGBB" or "RRGGBB". Returns false when the text is malformed.
    static bool fromHex(std::string_view text, Color& out);

    std::uint8_t red() const { return red_; }
    std::uint8_t green() const { return green_; }
    std::uint8_t blue() const { return blue_; }

    // Linear blend: ratio 0 -> *this, ratio 1 -> other.
    Color mix(const Color& other, double ratio) const;
    Color scale(double factor) const;

    bool operator==(const Color& other) const {
        return red_ == other.red_ && green_ == other.green_ && blue_ == other.blue_;
    }
    bool operator!=(const Color& other) const { return !(*this == other); }

private:
    std::uint8_t red_{0};
    std::uint8_t green_{0};
    std::uint8_t blue_{0};
};

inline std::uint8_t clampChannel(double value) {
    if (value <= 0.0) {
        return 0;
    }
    if (value >= 255.0) {
        return 255;
    }
    return static_cast<std::uint8_t>(value + 0.5);
}

inline int hexDigit(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + 10;
    }
    if (character >= 'A' && character <= 'F') {
        return character - 'A' + 10;
    }
    return -1;
}

inline bool Color::fromHex(std::string_view text, Color& out) {
    if (!text.empty() && text.front() == '#') {
        text.remove_prefix(1);
    }
    if (text.size() != 6) {
        return false;
    }
    int channels[3] = {0, 0, 0};
    for (int channel = 0; channel < 3; ++channel) {
        const int high = hexDigit(text[static_cast<std::size_t>(channel) * 2]);
        const int low = hexDigit(text[static_cast<std::size_t>(channel) * 2 + 1]);
        if (high < 0 || low < 0) {
            return false;
        }
        channels[channel] = high * 16 + low;
    }
    out = Color(static_cast<std::uint8_t>(channels[0]), static_cast<std::uint8_t>(channels[1]),
                static_cast<std::uint8_t>(channels[2]));
    return true;
}

inline Color Color::mix(const Color& other, double ratio) const {
    if (ratio <= 0.0) {
        return *this;
    }
    if (ratio >= 1.0) {
        return other;
    }
    return Color(clampChannel(red_ + (other.red_ - red_) * ratio),
                 clampChannel(green_ + (other.green_ - green_) * ratio),
                 clampChannel(blue_ + (other.blue_ - blue_) * ratio));
}

inline Color Color::scale(double factor) const {
    return Color(clampChannel(red_ * factor), clampChannel(green_ * factor),
                 clampChannel(blue_ * factor));
}

}  // namespace prts::model
