#include "service/boot_sequence_service.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "service/logo_art.h"
#include "utils/ansi.h"
#include "utils/text.h"
#include "utils/utf8.h"

namespace prts::service {
namespace {

constexpr int kDiamondRows = 9;
constexpr int kLabelWidth = 9;
constexpr int kStatusWidth = 6;
constexpr int kArtPaceMs = 26;
constexpr int kFlashMs = 55;
constexpr int kBarMaxWidth = 56;

std::string wordmark() { return "P  R  T  S"; }

std::string repeatCells(const char* cell, int count) {
    std::string result;
    for (int index = 0; index < count; ++index) {
        result.append(cell);
    }
    return result;
}

}  // namespace

BootSequenceService::BootSequenceService(AnimationService& animation, SelfcheckService& selfcheck,
                                         SessionStateService& sessionState,
                                         const model::BootSettings& settings,
                                         utils::Logger& logger)
    : animation_(animation), selfcheck_(selfcheck), sessionState_(sessionState),
      settings_(settings), logger_(logger) {}

bool BootSequenceService::useFullLayout() const {
    return animation_.columns() >= settings_.minColumnsFull &&
           animation_.rows() >= settings_.minRowsFull;
}

int BootSequenceService::contentWidth() const {
    const int width = std::min(animation_.columns() - 1, 74);
    return std::max(width, 40);
}

std::string BootSequenceService::paint(const std::string& text, const model::Color& color,
                                       bool bold) const {
    return animation_.paint(text, color, bold);
}

std::string BootSequenceService::padBetween(const std::string& left,
                                            const std::string& right) const {
    const int width = contentWidth();
    int space = width - static_cast<int>(utils::Text::displayWidth(left)) -
                static_cast<int>(utils::Text::displayWidth(right));
    if (space < 1) {
        space = 1;
    }
    return left + std::string(static_cast<std::size_t>(space), ' ') + right;
}

std::string BootSequenceService::twoDigits(int value) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return std::string(buffer);
}

std::string BootSequenceService::formatElapsed(std::int64_t milliseconds) {
    double seconds = static_cast<double>(milliseconds) / 1000.0;
    if (seconds > 99.99) {
        seconds = 99.99;
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%05.2f", seconds);
    return std::string(buffer);
}

std::string BootSequenceService::statusTag(model::CheckStatus status) {
    switch (status) {
        case model::CheckStatus::Ok:
            return "[ OK ]";
        case model::CheckStatus::Warn:
            return "[WARN]";
        case model::CheckStatus::Fail:
            return "[FAIL]";
    }
    return "[ ?? ]";
}

void BootSequenceService::renderRule() {
    animation_.writeStyled(repeatCells("\u2500", contentWidth()), settings_.palette.rule);
    animation_.newline();
}

void BootSequenceService::renderHeader() {
    const model::Palette& palette = settings_.palette;
    const model::Identity& identity = settings_.identity;
    const int width = contentWidth();

    animation_.writeStyled("\u258c", palette.accent, true);
    animation_.writeStyled(" ", palette.muted);
    animation_.writeStyled(identity.systemName, palette.primary, true);

    const std::string right = identity.systemTag + "  \u25e4";
    int space = width - 2 - static_cast<int>(utils::Text::displayWidth(identity.systemName)) -
                static_cast<int>(utils::Text::displayWidth(right));
    if (space < 1) {
        space = 1;
    }
    animation_.write(std::string(static_cast<std::size_t>(space), ' '));
    animation_.writeStyled(identity.systemTag, palette.muted);
    animation_.write("  ");
    animation_.writeStyled("\u25e4", palette.accent, true);
    animation_.newline();

    animation_.writeStyled("\u258c", palette.accent, true);
    animation_.writeStyled(" ", palette.muted);
    animation_.writeStyled(identity.systemFullName, palette.muted);

    const std::string sequence = "NO. " + twoDigits(sequenceNumber_);
    space = width - 2 - static_cast<int>(utils::Text::displayWidth(identity.systemFullName)) -
            static_cast<int>(utils::Text::displayWidth(sequence));
    if (space < 1) {
        space = 1;
    }
    animation_.write(std::string(static_cast<std::size_t>(space), ' '));
    animation_.writeStyled(sequence, palette.secondary);
    animation_.newline();

    renderRule();
}

void BootSequenceService::renderIdentity(bool animate) {
    const model::Palette& palette = settings_.palette;
    const LogoArt::Artwork artwork = LogoArt::diamond(kDiamondRows, wordmark(), contentWidth());

    for (std::size_t rowIndex = 0; rowIndex < artwork.rows.size(); ++rowIndex) {
        const std::string& row = artwork.rows[rowIndex];
        if (static_cast<int>(rowIndex) == artwork.wordmarkRow && artwork.wordmarkStart >= 0) {
            const std::vector<std::string> cells = utils::splitUtf8(row);
            std::string prefix;
            for (int index = 0; index < artwork.wordmarkStart; ++index) {
                prefix.append(cells[static_cast<std::size_t>(index)]);
            }
            animation_.writeStyled(prefix, palette.secondary);

            for (int index = 0; index < artwork.wordmarkLength; ++index) {
                const std::string& cell = cells[static_cast<std::size_t>(artwork.wordmarkStart + index)];
                animation_.writeStyled(cell, palette.primary, true);
                if (animate) {
                    animation_.flush();
                    animation_.sleep(std::max(4, settings_.charDelayMs * 2));
                }
            }

            std::string suffix;
            for (std::size_t index = static_cast<std::size_t>(artwork.wordmarkStart +
                                                              artwork.wordmarkLength);
                 index < cells.size(); ++index) {
                suffix.append(cells[index]);
            }
            animation_.writeStyled(suffix, palette.secondary);
            animation_.newline();
            continue;
        }

        animation_.writeStyled(row, palette.secondary);
        animation_.newline();
        if (animate) {
            animation_.flush();
            animation_.sleep(kArtPaceMs);
        }
    }

    const std::string caption =
        settings_.identity.systemLocalName + "  \u00b7  " + settings_.identity.accessNote;
    animation_.writeStyled(utils::Text::center(caption, static_cast<std::size_t>(contentWidth())),
                           palette.muted);
    animation_.newline();
}

void BootSequenceService::renderCompactIdentity(bool animate) {
    const model::Palette& palette = settings_.palette;
    const model::Identity& identity = settings_.identity;

    animation_.writeStyled(" \u25c6 ", palette.accent, true);
    animation_.writeStyled(identity.systemName, palette.primary, true);
    animation_.writeStyled(" \u25c6", palette.accent, true);
    animation_.write("   ");
    const std::string caption = identity.systemLocalName + "  \u00b7  " + identity.accessNote;
    if (animate) {
        animation_.typewriter(caption, palette.muted);
    } else {
        animation_.writeStyled(caption, palette.muted);
    }
    animation_.newline();
}

void BootSequenceService::renderCheckRow(const model::CheckResult& result, int number,
                                         bool animate) {
    const model::Palette& palette = settings_.palette;
    const int width = contentWidth();
    const std::string index = " " + twoDigits(number);
    const std::string label = utils::Text::fit(result.label, kLabelWidth);
    const int fixed = 3 + 3 + kLabelWidth + 1 + kStatusWidth + 1;
    const std::size_t valueWidth =
        static_cast<std::size_t>(std::max(8, width - fixed));
    const std::string value = utils::Text::fit(result.value, valueWidth);

    const std::string tag = statusTag(result.status);
    const std::string plain = index + "   " + label + " " + value + " " + tag;

    const model::Color statusColor = result.status == model::CheckStatus::Warn
                                         ? palette.accent
                                         : (result.status == model::CheckStatus::Fail
                                                ? palette.danger
                                                : palette.secondary);

    auto drawColored = [&](bool typeValue) {
        animation_.writeStyled(index, palette.subtle);
        animation_.write("   ");
        animation_.writeStyled(label, palette.muted);
        animation_.write(" ");
        if (animate && typeValue) {
            animation_.typewriter(value, palette.primary);
        } else {
            animation_.writeStyled(value, palette.primary);
        }
        animation_.write(" ");
        animation_.writeStyled("[", palette.subtle);
        animation_.writeStyled(tag.substr(1, tag.size() - 2), statusColor);
        animation_.writeStyled("]", palette.subtle);
    };

    drawColored(true);
    animation_.newline();
    if (!animate) {
        return;
    }

    animation_.sleep(std::max(10, settings_.checkDelayMs / 2));
    if (settings_.flashFrames > 0) {
        // The game marks the active element by reversing the block instead of
        // tinting it: draw the same row black on white for a single frame.
        animation_.moveUp(1);
        animation_.clearCurrentLine();
        animation_.write(utils::Ansi::inverse() + utils::Text::fit(plain, static_cast<std::size_t>(width)) +
                         utils::Ansi::reset());
        animation_.flush();
        animation_.sleep(kFlashMs);
        animation_.write(utils::Ansi::carriageReturn());
        animation_.clearCurrentLine();
        drawColored(false);
        animation_.newline();
    }
}

void BootSequenceService::renderChecks(const std::vector<model::CheckResult>& results,
                                       std::int64_t startedAt, bool animate) {
    (void)startedAt;
    int number = 1;
    for (const model::CheckResult& result : results) {
        renderCheckRow(result, number, animate);
        ++number;
    }
}

void BootSequenceService::renderProgress(double ratio, std::int64_t elapsedMs, bool animate) {
    const model::Palette& palette = settings_.palette;
    const int width = contentWidth();
    const int barWidth = std::max(10, std::min(kBarMaxWidth, width - 21));

    auto frame = [&](double value) {
        const double clamped = std::min(1.0, std::max(0.0, value));
        const int filled = static_cast<int>(std::lround(clamped * barWidth));
        std::string percent = utils::Text::formatFloat(clamped * 100.0, 0) + "%";
        while (static_cast<int>(percent.size()) < 4) {
            percent.insert(percent.begin(), ' ');
        }
        const std::string elapsed = formatElapsed(elapsedMs) + "s";
        const int used = 7 + barWidth;
        const int right = static_cast<int>(percent.size()) + 2 + static_cast<int>(elapsed.size());
        const int gap = std::max(1, width - used - right);

        std::string line = " " + paint("BOOT", palette.muted) + "  ";
        line += paint(repeatCells("\u2588", filled), palette.accent);
        line += paint(repeatCells("\u2591", barWidth - filled), palette.subtle);
        line += std::string(static_cast<std::size_t>(gap), ' ');
        line += paint(percent, palette.secondary);
        line += "  ";
        line += paint(elapsed, palette.subtle);
        animation_.overwriteLine(line);
    };

    if (!animate) {
        frame(1.0);
    } else {
        constexpr int kSteps = 14;
        for (int step = 0; step <= kSteps; ++step) {
            frame(static_cast<double>(step) / static_cast<double>(kSteps));
            animation_.sleep(std::max(8, settings_.frameDelayMs));
        }
    }
    animation_.newline();
}

void BootSequenceService::renderVerdict(const std::vector<model::CheckResult>& results,
                                        bool animate) {
    const model::Palette& palette = settings_.palette;
    const int okCount = SelfcheckService::countByStatus(results, model::CheckStatus::Ok);
    const int warnCount = SelfcheckService::countByStatus(results, model::CheckStatus::Warn);
    const int failCount = SelfcheckService::countByStatus(results, model::CheckStatus::Fail);

    std::string chinese = "\u5168\u90e8\u6a21\u5757\u54cd\u5e94\u6b63\u5e38";
    std::string latin = "ALL MODULES NOMINAL";
    model::Color color = palette.primary;
    if (failCount > 0) {
        chinese = "\u68c0\u6d4b\u5230\u7cfb\u7edf\u6545\u969c";
        latin = "SYSTEM FAULT DETECTED";
        color = palette.danger;
    } else if (warnCount > 0) {
        chinese = "\u7cfb\u7edf\u6b63\u5e38\uff0c\u5b58\u5728\u544a\u8b66";
        latin = "NOMINAL WITH WARNINGS";
        color = palette.accent;
    }

    const std::string ratio =
        std::to_string(okCount) + " / " + std::to_string(results.size());

    animation_.write(" ");
    animation_.writeStyled("\u25c6", palette.accent, true);
    animation_.write(" ");
    if (animate) {
        animation_.typewriter(chinese, color);
    } else {
        animation_.writeStyled(chinese, color);
    }
    animation_.write("   ");
    animation_.writeStyled(latin, palette.muted);

    const int used = 1 + 1 + 1 + static_cast<int>(utils::Text::displayWidth(chinese)) + 3 +
                     static_cast<int>(utils::Text::displayWidth(latin));
    const int gap = std::max(1, contentWidth() - used -
                                    static_cast<int>(utils::Text::displayWidth(ratio)));
    animation_.write(std::string(static_cast<std::size_t>(gap), ' '));
    animation_.writeStyled(ratio, palette.secondary);
    animation_.newline();
}

void BootSequenceService::renderWelcome(const SessionStateService::State& state, bool animate) {
    const model::Palette& palette = settings_.palette;
    const std::string& operatorName = settings_.identity.operatorName;

    std::string text;
    if (state.lastLogin.empty()) {
        text = "\u63a5\u5165\u8bb0\u5f55\uff1a" + operatorName +
               " \u00b7 \u9996\u6b21\u63a5\u5165\u3002PRTS \u5df2\u5efa\u7acb\u7ec8\u7aef\u6863\u6848\u3002";
    } else {
        text = "\u63a5\u5165\u8bb0\u5f55\uff1a" + operatorName + " \u00b7 \u7b2c " +
               std::to_string(sequenceNumber_) +
               " \u6b21\u3002\u4e0a\u6b21\u63a5\u5165 " + state.lastLogin +
               "\u3002PRTS \u5df2\u5c31\u7eea\uff0c\u7b49\u5f85\u6307\u4ee4\u3002";
    }

    animation_.write(" ");
    if (animate) {
        animation_.typewriter(text, palette.muted, false, std::max(3, settings_.charDelayMs / 2));
    } else {
        animation_.writeStyled(text, palette.muted);
    }
    animation_.newline();
}

void BootSequenceService::renderStatusStrip(const model::SystemFacts& facts) {
    const model::Palette& palette = settings_.palette;
    std::vector<std::pair<std::string, std::string>> items;

    if (facts.cpuTemperatureC >= 0.0) {
        items.emplace_back("CPU", utils::Text::formatFloat(facts.cpuTemperatureC, 1) + " C");
    }
    items.emplace_back("MEM", utils::Text::formatFloat(facts.memoryUsedPercent(), 0) + "%");
    items.emplace_back("DISK", utils::Text::formatFloat(facts.diskUsedPercent(), 0) + "%");
    items.emplace_back("LOAD", utils::Text::formatFloat(facts.loadAverage1, 2));
    items.emplace_back("UP", utils::Text::formatDuration(facts.uptimeSeconds));

    animation_.write(" ");
    animation_.writeStyled("\u25b8", palette.accent, true);
    for (std::size_t index = 0; index < items.size(); ++index) {
        animation_.write(" ");
        animation_.writeStyled(items[index].first, palette.muted);
        animation_.write(" ");
        animation_.writeStyled(items[index].second, palette.secondary);
        if (index + 1 != items.size()) {
            animation_.write("  ");
        }
    }
    animation_.newline();
}

BootSequenceService::Outcome BootSequenceService::run(bool animate) {
    const std::int64_t startedAt = animation_.nowMs();

    facts_ = model::SystemFacts();
    const std::vector<model::CheckResult> results = selfcheck_.run(facts_);
    const SessionStateService::State previous = sessionState_.read();
    sequenceNumber_ = static_cast<int>(previous.loginCount) + 1;

    if (animate) {
        animation_.begin();
    }

    renderHeader();
    animation_.newline();

    if (useFullLayout()) {
        renderIdentity(animate);
    } else {
        renderCompactIdentity(animate);
    }
    renderRule();
    renderChecks(results, startedAt, animate);
    animation_.newline();

    renderProgress(1.0, animation_.nowMs() - startedAt, animate);
    renderVerdict(results, animate);
    renderWelcome(previous, animate);
    renderStatusStrip(facts_);

    if (animate) {
        animation_.end();
    }
    animation_.flush();

    sessionState_.record(animation_.wallClock("%Y-%m-%d %H:%M:%S"));

    Outcome outcome;
    outcome.okCount = SelfcheckService::countByStatus(results, model::CheckStatus::Ok);
    outcome.warnCount = SelfcheckService::countByStatus(results, model::CheckStatus::Warn);
    outcome.failCount = SelfcheckService::countByStatus(results, model::CheckStatus::Fail);
    outcome.durationMs = animation_.nowMs() - startedAt;
    logger_.info("boot sequence finished in " + std::to_string(outcome.durationMs) + " ms");
    return outcome;
}

}  // namespace prts::service
