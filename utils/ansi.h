#pragma once

#include <string>

#include "model/color.h"

namespace prts::utils {

// Builds ANSI/VT escape sequences. Kept free of state so that any layer may
// describe styling without knowing which terminal driver will emit it.
class Ansi {
public:
    // Text styling
    static std::string reset();
    static std::string bold();
    static std::string dim();
    static std::string fg(const model::Color& color);
    static std::string fgBold(const model::Color& color);

    // Cursor and screen control
    static std::string hideCursor();
    static std::string showCursor();
    static std::string clearScreen();
    static std::string home();
    static std::string cursorUp(int lines);
    static std::string clearLine();
    // Erases from the cursor to the end of the line, keeping what was printed.
    static std::string eraseToEndOfLine();
    // Swaps foreground and background: the game uses this instead of colours to
    // mark the active element.
    static std::string inverse();
    static std::string carriageReturn();    static std::string moveTo(int column, int row);

    // Palette used by the boot sequence is defined once, here, so that every
    // screen shares the same visual language.
    static std::string styled(const std::string& text, const model::Color& color,
                              bool boldText = false);
};

}  // namespace prts::utils
