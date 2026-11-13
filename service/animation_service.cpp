#include "service/animation_service.h"

#include "utils/ansi.h"
#include "utils/utf8.h"

namespace prts::service {

AnimationService::AnimationService(driver::ITerminal& terminal, driver::IClock& clock,
                                   const model::BootSettings& settings, utils::Logger& logger)
    : terminal_(terminal), clock_(clock), settings_(settings), logger_(logger) {
    const driver::TerminalSize size = terminal_.size();
    logger_.debug("terminal size: " + std::to_string(size.columns) + "x" +
                  std::to_string(size.rows) + " colour=" +
                  (terminal_.supportsColor() ? "on" : "off"));
}

int AnimationService::columns() const { return terminal_.size().columns; }

int AnimationService::rows() const { return terminal_.size().rows; }

void AnimationService::begin() {
    terminal_.setCursorVisible(false);
    terminal_.clearScreen();
    terminal_.flush();
}

void AnimationService::end() {
    terminal_.setCursorVisible(true);
    terminal_.flush();
}

void AnimationService::write(const std::string& text) { terminal_.write(text); }

void AnimationService::writeStyled(const std::string& text, const model::Color& color,
                                   bool boldText) {
    terminal_.writeStyled(text, color, boldText);
}

void AnimationService::writeCharStyled(const std::string& utf8Char, const model::Color& color,
                                       bool boldText) {
    terminal_.writeStyled(utf8Char, color, boldText);
}

void AnimationService::newline(int count) {
    for (int index = 0; index < count; ++index) {
        terminal_.write("\n");
    }
}

void AnimationService::moveUp(int lines) { terminal_.moveUp(lines); }

void AnimationService::clearCurrentLine() { terminal_.clearLine(); }

void AnimationService::overwriteLine(const std::string& text) {
    terminal_.write(utils::Ansi::carriageReturn());
    terminal_.write(text);
    if (terminal_.isInteractive()) {
        terminal_.write(utils::Ansi::eraseToEndOfLine());
    }
    terminal_.flush();
}

std::string AnimationService::paint(const std::string& text, const model::Color& color,
                                    bool boldText) const {
    if (!terminal_.supportsColor()) {
        return text;
    }
    return utils::Ansi::styled(text, color, boldText);
}

void AnimationService::flush() { terminal_.flush(); }

void AnimationService::typewriter(const std::string& text, const model::Color& color,
                                  bool boldText, int delayMs) {
    const int delay = delayMs >= 0 ? delayMs : settings_.charDelayMs;
    const auto pieces = utils::splitUtf8(text);
    for (const std::string& piece : pieces) {
        terminal_.writeStyled(piece, color, boldText);
        if (delay > 0) {
            terminal_.flush();
            clock_.sleepMillis(delay);
        }
    }
    terminal_.flush();
}

void AnimationService::typewriterLine(const std::string& text, const model::Color& color,
                                      bool boldText, int delayMs) {
    typewriter(text, color, boldText, delayMs);
    newline();
}

void AnimationService::sleep(int milliseconds) {
    if (milliseconds > 0) {
        clock_.sleepMillis(milliseconds);
    }
}

}  // namespace prts::service
