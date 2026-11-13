#pragma once

#include <cstdint>
#include <string>

#include "driver/i_clock.h"
#include "driver/i_terminal.h"
#include "model/boot_settings.h"
#include "utils/logger.h"

namespace prts::service {

// Provides the animation primitives (typing, overwriting a line, cursor
// movement) on top of the terminal abstraction. Layout decisions stay in the
// sequence services; this class only knows how to emit paced text.
class AnimationService {
public:
    AnimationService(driver::ITerminal& terminal, driver::IClock& clock,
                     const model::BootSettings& settings, utils::Logger& logger);

    void begin();
    void end();

    int columns() const;
    int rows() const;
    bool supportsColor() const { return terminal_.supportsColor(); }

    void write(const std::string& text);
    void writeStyled(const std::string& text, const model::Color& color, bool boldText = false);
    void writeCharStyled(const std::string& utf8Char, const model::Color& color,
                         bool boldText = false);
    void newline(int count = 1);
    void moveUp(int lines);
    void clearCurrentLine();
    void overwriteLine(const std::string& text);

    void typewriter(const std::string& text, const model::Color& color, bool boldText = false,
                    int delayMs = -1);
    void typewriterLine(const std::string& text, const model::Color& color, bool boldText = false,
                        int delayMs = -1);

    // Wraps the text in colour codes, or returns it unchanged when the terminal
    // cannot render colour.
    std::string paint(const std::string& text, const model::Color& color, bool boldText = false) const;

    void flush();
    bool isInteractive() const { return terminal_.isInteractive(); }
    std::string wallClock(const std::string& format) const { return clock_.wallClock(format); }

    void sleep(int milliseconds);
    std::int64_t nowMs() const { return clock_.steadyMillis(); }

    int charDelay() const { return settings_.charDelayMs; }
    int frameDelay() const { return settings_.frameDelayMs; }
    int checkDelay() const { return settings_.checkDelayMs; }
    const model::Palette& palette() const { return settings_.palette; }
    const model::Identity& identity() const { return settings_.identity; }

private:
    driver::ITerminal& terminal_;
    driver::IClock& clock_;
    const model::BootSettings& settings_;
    utils::Logger& logger_;
};

}  // namespace prts::service
