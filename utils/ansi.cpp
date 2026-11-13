#include "utils/ansi.h"

#include <string>

namespace prts::utils {
namespace {

std::string colorToSgr(const model::Color& color) {
    return std::to_string(static_cast<int>(color.red())) + ";" +
           std::to_string(static_cast<int>(color.green())) + ";" +
           std::to_string(static_cast<int>(color.blue()));
}

}  // namespace

std::string Ansi::reset() { return "\033[0m"; }

std::string Ansi::bold() { return "\033[1m"; }

std::string Ansi::dim() { return "\033[2m"; }

std::string Ansi::fg(const model::Color& color) { return "\033[38;2;" + colorToSgr(color) + "m"; }

std::string Ansi::fgBold(const model::Color& color) {
    return "\033[1m\033[38;2;" + colorToSgr(color) + "m";
}

std::string Ansi::hideCursor() { return "\033[?25l"; }

std::string Ansi::showCursor() { return "\033[?25h"; }

std::string Ansi::clearScreen() { return "\033[2J\033[H"; }

std::string Ansi::home() { return "\033[H"; }

std::string Ansi::cursorUp(int lines) {
    if (lines <= 0) {
        return std::string();
    }
    return "\033[" + std::to_string(lines) + "A";
}

std::string Ansi::clearLine() { return "\033[2K"; }

std::string Ansi::eraseToEndOfLine() { return "\033[K"; }

std::string Ansi::inverse() { return "\033[7m"; }

std::string Ansi::carriageReturn() { return "\r"; }

std::string Ansi::moveTo(int column, int row) {
    return "\033[" + std::to_string(row + 1) + ";" + std::to_string(column + 1) + "H";
}

std::string Ansi::styled(const std::string& text, const model::Color& color, bool boldText) {
    return (boldText ? fgBold(color) : fg(color)) + text + reset();
}

}  // namespace prts::utils
