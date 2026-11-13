#include "service/boot_sequence_service.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "service/logo_art.h"
#include "utils/text.h"
#include "utils/utf8.h"

namespace prts::service {
namespace {

constexpr int kNoHighlight = -10000;
constexpr int kLabelWidth = 10;
constexpr int kElapsedWidth = 8;  // "[00.000]"
constexpr int kStatusWidth = 6;   // "[ OK ]"
constexpr int kArtPaceMs = 32;
constexpr double kSweepHalfBand = 2.0;

std::string statusTag(model::CheckStatus status) {
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

model::Color statusColor(const model::Palette& palette, model::CheckStatus status) {
    switch (status) {
        case model::CheckStatus::Ok:
            return palette.ok;
        case model::CheckStatus::Warn:
            return palette.warn;
        case model::CheckStatus::Fail:
            return palette.error;
    }
    return palette.primary;
}

}  // namespace

BootSequenceService::BootSequenceService(AnimationService& animation, SelfcheckService& selfcheck,
                                         SessionStateService& sessionState,
                                         const model::BootSettings& settings,
                                         utils::Logger& logger)
    : animation_(animation), selfcheck_(selfcheck), sessionState_(sessionState),
      settings_(settings), logger_(logger) {}

BootSequenceService::Density BootSequenceService::chooseDensity() const {
    if (animation_.columns() >= settings_.minColumnsFull &&
        animation_.rows() >= settings_.minRowsFull) {
        return Density::Full;
    }
    return Density::Compact;
}

int BootSequenceService::contentWidth() const {
    const int width = std::min(animation_.columns() - 2, 76);
    return std::max(width, 30);
}

int BootSequenceService::contentRows() const { return animation_.rows(); }

std::string BootSequenceService::formatElapsed(std::int64_t milliseconds) {
    double seconds = static_cast<double>(milliseconds) / 1000.0;
    if (seconds > 99.999) {
        seconds = 99.999;
    }
    char buffer[16];
    std::snprintf(buffer, sizeof(buffer), "%06.3f", seconds);
    return std::string(buffer);
}

void BootSequenceService::renderHeader() {
    const model::Palette& palette = animation_.palette();
    const model::Identity& identity = animation_.identity();
    const int inner = contentWidth();
    const std::string horizontal = utils::Text::repeat("\u2500", static_cast<std::size_t>(inner));

    animation_.writeStyled("\u250c" + horizontal + "\u2510", palette.frameDim);
    animation_.newline();

    const std::string name = identity.systemName;
    std::string link = "LINK: ACTIVE";
    const int nameWidth = static_cast<int>(utils::Text::displayWidth(name));
    const int prefixWidth = 1 + nameWidth + 5;  // space + name + "  \u25b8  "
    int linkWidth = static_cast<int>(utils::Text::displayWidth(link));
    int fullBudget = inner - prefixWidth - linkWidth - 1;
    if (fullBudget < 6) {
        link = "ACTIVE";
        linkWidth = static_cast<int>(utils::Text::displayWidth(link));
        fullBudget = inner - prefixWidth - linkWidth - 1;
    }
    if (fullBudget < 0) {
        fullBudget = 0;
    }
    const std::string fullShown = utils::Text::truncate(identity.systemFullName, static_cast<std::size_t>(fullBudget));
    int gap = inner - prefixWidth - static_cast<int>(utils::Text::displayWidth(fullShown)) -
              linkWidth - 1;
    if (gap < 0) {
        gap = 0;
    }

    animation_.writeStyled("\u2502", palette.frameDim);
    animation_.write(" ");
    animation_.writeStyled(name, palette.accentBright, true);
    animation_.writeStyled("  \u25b8  ", palette.frame, true);
    animation_.writeStyled(fullShown, palette.primary);
    animation_.write(std::string(static_cast<std::size_t>(gap), ' '));
    animation_.writeStyled(link, palette.ok, true);
    animation_.write(" ");
    animation_.writeStyled("\u2502", palette.frameDim);
    animation_.newline();

    animation_.writeStyled("\u2514" + horizontal + "\u2518", palette.frameDim);
    animation_.newline();
}

void BootSequenceService::renderBlankLine() { animation_.newline(); }

void BootSequenceService::drawGrid(const std::vector<std::string>& rows, int highlightColumn,
                                   const model::Color& topColor, const model::Color& bottomColor,
                                   bool paced) {
    const int inner = contentWidth();
    const int artWidth = LogoArt::gridWidth(rows);
    const int leftPad = std::max(0, (inner - artWidth) / 2);

    for (std::size_t rowIndex = 0; rowIndex < rows.size(); ++rowIndex) {
        const double position =
            rows.size() > 1 ? static_cast<double>(rowIndex) / static_cast<double>(rows.size() - 1)
                            : 0.0;
        const model::Color baseColor = topColor.mix(bottomColor, position * 0.9);

        animation_.write(std::string(static_cast<std::size_t>(leftPad), ' '));

        // Consecutive cells that share a style are emitted as one run: the
        // sweep redraws the grid on every frame, so batching keeps the byte
        // count (and the Pi's CPU load) low.
        std::string run;
        model::Color runColor = baseColor;
        bool runBold = false;
        auto flushRun = [this, &run, &runColor, &runBold]() {
            if (!run.empty()) {
                animation_.writeStyled(run, runColor, runBold);
                run.clear();
            }
        };

        const std::vector<std::string> cells = utils::splitUtf8(rows[rowIndex]);
        for (std::size_t cellIndex = 0; cellIndex < cells.size(); ++cellIndex) {
            const std::string& cell = cells[cellIndex];
            if (cell == " ") {
                flushRun();
                animation_.write(" ");
                continue;
            }
            const bool highlighted =
                highlightColumn != kNoHighlight &&
                std::fabs(static_cast<double>(static_cast<int>(cellIndex) - highlightColumn)) <=
                    kSweepHalfBand;
            const model::Color color = highlighted ? animation_.palette().accentBright : baseColor;
            if (color != runColor || highlighted != runBold) {
                flushRun();
                runColor = color;
                runBold = highlighted;
            }
            run.append(cell);
        }
        flushRun();
        animation_.newline();
        if (paced) {
            animation_.flush();
            animation_.sleep(kArtPaceMs);
        }
    }
}

void BootSequenceService::drawEmblemFrame(int highlightColumn, bool paced) {
    const model::Palette& palette = animation_.palette();
    drawGrid(LogoArt::emblem(), highlightColumn, palette.accentBright, palette.accent, paced);
}

void BootSequenceService::drawWordmarkFrame(int highlightColumn, bool paced) {
    const model::Palette& palette = animation_.palette();
    drawGrid(LogoArt::wordmark(), highlightColumn, palette.primary, palette.muted, paced);
}

void BootSequenceService::renderArtwork(bool animate) {
    drawEmblemFrame(kNoHighlight, true);
    drawWordmarkFrame(kNoHighlight, true);

    if (!animate) {
        return;
    }

    const int totalRows = static_cast<int>(LogoArt::emblem().size() +
                                           LogoArt::wordmark().size());
    const int artWidth = std::max(LogoArt::gridWidth(LogoArt::emblem()),
                                  LogoArt::gridWidth(LogoArt::wordmark()));
    const int sweeps = std::max(0, settings_.scanSweeps);
    for (int sweep = 0; sweep < sweeps; ++sweep) {
        for (int column = -4; column <= artWidth + 4; column += 3) {
            animation_.moveUp(totalRows);
            drawEmblemFrame(column, false);
            drawWordmarkFrame(column, false);
            animation_.flush();
            animation_.sleep(settings_.frameDelayMs);
        }
    }
    logger_.debug("artwork drawn, sweeps=" + std::to_string(sweeps));
}

void BootSequenceService::renderCompactLogo() {
    const model::Palette& palette = animation_.palette();
    const model::Identity& identity = animation_.identity();
    animation_.writeStyled("  " + LogoArt::compactMark(identity.systemName), palette.accentBright,
                           true);
    animation_.writeStyled("   " + identity.systemLocalName, palette.muted);
    animation_.newline();
}

void BootSequenceService::renderCheckRow(const model::CheckResult& result, std::int64_t elapsedMs,
                                        bool animate) {
    const model::Palette& palette = animation_.palette();
    const int inner = contentWidth();
    const int dotsWidth = chooseDensity() == Density::Full ? 12 : 4;
    const int fixedWidth = 2 + kElapsedWidth + 2 + kLabelWidth + 2 + dotsWidth + 2 + 1 + kStatusWidth;
    const std::size_t valueWidth =
        static_cast<std::size_t>(std::max(8, inner - fixedWidth));

    animation_.write("  ");
    animation_.writeStyled("[" + formatElapsed(elapsedMs) + "]", palette.frameDim);
    animation_.write("  ");
    animation_.writeStyled(utils::Text::fit(result.label, kLabelWidth), palette.muted, true);
    animation_.write("  ");
    animation_.writeStyled(utils::Text::repeat(".", static_cast<std::size_t>(dotsWidth)),
                           palette.frameDim);
    animation_.write("  ");

    const std::string value = utils::Text::fit(result.value, valueWidth);
    if (animate) {
        animation_.typewriter(value, palette.primary);
        animation_.sleep(std::max(0, settings_.checkDelayMs / 2));
    } else {
        animation_.writeStyled(value, palette.primary);
    }

    animation_.write(" ");
    animation_.writeStyled(statusTag(result.status), statusColor(palette, result.status), true);
    animation_.newline();
    if (animate) {
        animation_.sleep(std::max(0, settings_.checkDelayMs / 2));
    }
}

void BootSequenceService::renderChecks(const std::vector<model::CheckResult>& results,
                                       std::int64_t startedAt, bool animate) {
    for (const model::CheckResult& result : results) {
        renderCheckRow(result, animation_.nowMs() - startedAt, animate);
    }
}

void BootSequenceService::renderProgress(bool animate) {
    const model::Palette& palette = animation_.palette();
    const int inner = contentWidth();
    const int barWidth = std::max(10, std::min(46, inner - 20));
    const std::string label = "BOOT SEQUENCE";

    auto frame = [&](double value) {
        const double clamped = std::min(1.0, std::max(0.0, value));
        const int filled = static_cast<int>(std::lround(clamped * barWidth));
        std::string percent = utils::Text::formatFloat(clamped * 100.0, 0) + "%";
        while (utils::Text::displayWidth(percent) < 4) {
            percent.insert(percent.begin(), ' ');
        }
        const std::string line =
            "  " + animation_.paint(label, palette.muted) + " " +
            animation_.paint("[", palette.frameDim) +
            animation_.paint(utils::Text::repeat("\u2588", static_cast<std::size_t>(filled)),
                             palette.accent) +
            animation_.paint(utils::Text::repeat("\u2591", static_cast<std::size_t>(barWidth - filled)),
                             palette.frameDim) +
            animation_.paint("]", palette.frameDim) + " " +
            animation_.paint(percent, palette.primary, true);
        animation_.overwriteLine(line);
    };

    if (!animate) {
        frame(1.0);
    } else {
        constexpr int kSteps = 12;
        for (int step = 0; step <= kSteps; ++step) {
            frame(static_cast<double>(step) / static_cast<double>(kSteps));
            animation_.sleep(std::max(8, settings_.frameDelayMs));
        }
    }
    animation_.newline();
}

void BootSequenceService::renderSummary(const std::vector<model::CheckResult>& results,
                                        bool animate) {
    const model::Palette& palette = animation_.palette();
    const int inner = contentWidth();
    const int okCount = SelfcheckService::countByStatus(results, model::CheckStatus::Ok);
    const int warnCount = SelfcheckService::countByStatus(results, model::CheckStatus::Warn);
    const int failCount = SelfcheckService::countByStatus(results, model::CheckStatus::Fail);

    std::string verdict = "ALL SYSTEMS NOMINAL";
    std::string verdictLocal = "\u5168\u90e8\u7cfb\u7edf\u6b63\u5e38";
    model::Color color = palette.ok;
    if (failCount > 0) {
        verdict = "SYSTEM FAULT DETECTED";
        verdictLocal = "\u68c0\u6d4b\u5230\u7cfb\u7edf\u6545\u969c";
        color = palette.error;
    } else if (warnCount > 0) {
        verdict = "NOMINAL WITH WARNINGS";
        verdictLocal = "\u7cfb\u7edf\u6b63\u5e38\uff0c\u5b58\u5728\u544a\u8b66";
        color = palette.warn;
    }

    const std::string right = std::to_string(okCount) + " / " +
                              std::to_string(results.size()) + " PASSED";
    const int used = 2 + static_cast<int>(utils::Text::displayWidth("\u25c6 ")) +
                     static_cast<int>(utils::Text::displayWidth(verdict)) +
                     static_cast<int>(utils::Text::displayWidth("  /  ")) +
                     static_cast<int>(utils::Text::displayWidth(verdictLocal));
    int gap = inner - used - static_cast<int>(utils::Text::displayWidth(right));
    if (gap < 1) {
        gap = 1;
    }

    animation_.write("  ");
    animation_.writeStyled("\u25c6 ", color, true);
    if (animate) {
        animation_.typewriter(verdict, color, true);
        animation_.writeStyled("  /  ", palette.muted);
        animation_.typewriter(verdictLocal, palette.primary);
    } else {
        animation_.writeStyled(verdict, color, true);
        animation_.writeStyled("  /  ", palette.muted);
        animation_.writeStyled(verdictLocal, palette.primary);
    }
    animation_.write(std::string(static_cast<std::size_t>(gap), ' '));
    animation_.writeStyled(right, color);
    animation_.newline();
}

void BootSequenceService::renderWelcome(const SessionStateService::State& state, bool animate) {
    const model::Palette& palette = animation_.palette();
    const model::Identity& identity = animation_.identity();

    std::string text;
    if (state.lastLogin.empty()) {
        text = "\u6b22\u8fce\u63a5\u5165\uff0c" + identity.operatorName +
               "\u3002PRTS \u5df2\u4e3a\u8be5\u7ec8\u7aef\u5efa\u7acb\u4f1a\u8bdd\u6863\u6848\u3002";
    } else {
        text = "\u6b22\u8fce\u56de\u6765\uff0c" + identity.operatorName + "\u3002\u7b2c " +
               std::to_string(state.loginCount + 1) + " \u6b21\u63a5\u5165 \u00b7 \u4e0a\u6b21\u767b\u5f55 " +
               state.lastLogin;
    }

    animation_.write("  ");
    if (animate) {
        animation_.typewriter(text, palette.muted, false, std::max(3, settings_.charDelayMs / 2));
    } else {
        animation_.writeStyled(text, palette.muted);
    }
    animation_.newline();
}

void BootSequenceService::renderStatusStrip(const model::SystemFacts& facts) {
    const model::Palette& palette = animation_.palette();
    std::vector<std::pair<std::string, std::string>> items;

    if (facts.cpuTemperatureC >= 0.0) {
        items.emplace_back("CPU", utils::Text::formatFloat(facts.cpuTemperatureC, 1) + " C");
    }
    items.emplace_back("MEM", utils::Text::formatFloat(facts.memoryUsedPercent(), 0) + "%");
    items.emplace_back("DISK", utils::Text::formatFloat(facts.diskUsedPercent(), 0) + "%");
    items.emplace_back("LOAD", utils::Text::formatFloat(facts.loadAverage1, 2));
    items.emplace_back("UP", utils::Text::formatDuration(facts.uptimeSeconds));

    animation_.write("  ");
    for (std::size_t index = 0; index < items.size(); ++index) {
        if (index != 0) {
            animation_.writeStyled("   ", palette.frameDim);
        }
        animation_.writeStyled(items[index].first + " ", palette.muted);
        animation_.writeStyled(items[index].second, palette.primary);
    }
    animation_.newline();
}

BootSequenceService::Outcome BootSequenceService::run(bool animate) {
    const std::int64_t startedAt = animation_.nowMs();

    facts_ = model::SystemFacts();
    const std::vector<model::CheckResult> results = selfcheck_.run(facts_);
    const SessionStateService::State previous = sessionState_.read();

    if (animate) {
        animation_.begin();
    }

    renderHeader();
    if (chooseDensity() == Density::Full) {
        renderBlankLine();
        renderArtwork(animate);
    } else {
        renderCompactLogo();
    }

    renderChecks(results, startedAt, animate);
    renderProgress(animate);
    renderBlankLine();
    renderSummary(results, animate);
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
