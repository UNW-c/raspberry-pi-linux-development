#pragma once

#include <string>
#include <string_view>

#include "model/color.h"

namespace prts::driver {

struct TerminalSize {
    int columns{80};
    int rows{24};
};

// Abstraction over the output device. The animation layer only ever talks to
// this interface, which keeps it testable and independent of ANSI specifics.
class ITerminal {
public:
    virtual ~ITerminal() = default;

    virtual void write(std::string_view text) = 0;
    virtual void writeStyled(std::string_view text, const model::Color& color,
                             bool boldText = false) = 0;

    virtual void clearScreen() = 0;
    virtual void moveUp(int lines) = 0;
    virtual void clearLine() = 0;
    virtual void setCursorVisible(bool visible) = 0;
    virtual void flush() = 0;

    virtual TerminalSize size() const = 0;
    virtual bool isInteractive() const = 0;
    virtual bool supportsColor() const = 0;
};

}  // namespace prts::driver
