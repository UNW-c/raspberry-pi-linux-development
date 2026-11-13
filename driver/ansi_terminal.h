#pragma once

#include <string>

#include "driver/i_terminal.h"

namespace prts::driver {

// ANSI/VT100 implementation of ITerminal. Handles terminal capability
// detection (TTY, colour support) and nothing else.
class AnsiTerminal final : public ITerminal {
public:
    AnsiTerminal();

    void write(std::string_view text) override;
    void writeStyled(std::string_view text, const model::Color& color,
                     bool boldText = false) override;

    void clearScreen() override;
    void moveUp(int lines) override;
    void clearLine() override;
    void setCursorVisible(bool visible) override;
    void flush() override;

    TerminalSize size() const override;
    bool isInteractive() const override { return interactive_; }
    bool supportsColor() const override { return colorEnabled_; }

    // Escape hatches used by the CLI options.
    void setColorEnabled(bool enabled) { colorEnabled_ = enabled; }

private:
    bool interactive_{false};
    bool colorEnabled_{false};
};

}  // namespace prts::driver
