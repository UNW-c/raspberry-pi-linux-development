#include "driver/ansi_terminal.h"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "utils/ansi.h"

namespace prts::driver {
namespace {

bool environmentFlagSet(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::strcmp(value, "0") != 0;
}

}  // namespace

AnsiTerminal::AnsiTerminal() {
    interactive_ = ::isatty(STDOUT_FILENO) == 1;

    const char* term = std::getenv("TERM");
    const bool dumbTerminal = term == nullptr || std::strcmp(term, "dumb") == 0;
    colorEnabled_ = interactive_ && !dumbTerminal && !environmentFlagSet("NO_COLOR");
}

void AnsiTerminal::write(std::string_view text) {
    if (!text.empty()) {
        std::fwrite(text.data(), 1, text.size(), stdout);
    }
}

void AnsiTerminal::writeStyled(std::string_view text, const model::Color& color, bool boldText) {
    if (!colorEnabled_) {
        write(text);
        return;
    }
    std::string fragment = utils::Ansi::styled(std::string(text), color, boldText);
    write(fragment);
}

void AnsiTerminal::clearScreen() {
    if (interactive_) {
        write(utils::Ansi::clearScreen());
    }
}

void AnsiTerminal::moveUp(int lines) {
    if (interactive_ && lines > 0) {
        write(utils::Ansi::cursorUp(lines));
    }
}

void AnsiTerminal::clearLine() {
    if (interactive_) {
        write(utils::Ansi::clearLine());
    }
}

void AnsiTerminal::setCursorVisible(bool visible) {
    if (interactive_) {
        write(visible ? utils::Ansi::showCursor() : utils::Ansi::hideCursor());
    }
}

void AnsiTerminal::flush() { std::fflush(stdout); }

TerminalSize AnsiTerminal::size() const {
    TerminalSize result;
    winsize window{};
    if (::ioctl(STDOUT_FILENO, TIOCGWINSZ, &window) == 0 && window.ws_col > 0 &&
        window.ws_row > 0) {
        result.columns = static_cast<int>(window.ws_col);
        result.rows = static_cast<int>(window.ws_row);
        return result;
    }
    if (const char* columns = std::getenv("COLUMNS"); columns != nullptr) {
        const int parsed = std::atoi(columns);
        if (parsed > 0) {
            result.columns = parsed;
        }
    }
    if (const char* rows = std::getenv("LINES"); rows != nullptr) {
        const int parsed = std::atoi(rows);
        if (parsed > 0) {
            result.rows = parsed;
        }
    }
    return result;
}

}  // namespace prts::driver
