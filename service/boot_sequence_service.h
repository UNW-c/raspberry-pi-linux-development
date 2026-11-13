#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "model/boot_settings.h"
#include "model/check_result.h"
#include "model/system_facts.h"
#include "service/animation_service.h"
#include "service/selfcheck_service.h"
#include "service/session_state_service.h"
#include "utils/logger.h"

namespace prts::service {

// Composes the PRTS style login sequence: header panel, emblem, wordmark, the
// seven self-check rows, the boot bar and the closing summary.
class BootSequenceService {
public:
    struct Outcome {
        int okCount{0};
        int warnCount{0};
        int failCount{0};
        std::int64_t durationMs{0};
    };

    BootSequenceService(AnimationService& animation, SelfcheckService& selfcheck,
                        SessionStateService& sessionState, const model::BootSettings& settings,
                        utils::Logger& logger);

    Outcome run(bool animate);

private:
    enum class Density {
        Full,
        Compact,
    };

    Density chooseDensity() const;
    int contentWidth() const;
    int contentRows() const;

    void renderHeader();
    void renderArtwork(bool animate);
    void renderCompactLogo();
    void renderChecks(const std::vector<model::CheckResult>& results, std::int64_t startedAt,
                      bool animate);
    void renderCheckRow(const model::CheckResult& result, std::int64_t elapsedMs, bool animate);
    void renderProgress(bool animate);
    void renderSummary(const std::vector<model::CheckResult>& results, bool animate);
    void renderWelcome(const SessionStateService::State& state, bool animate);
    void renderStatusStrip(const model::SystemFacts& facts);
    void renderBlankLine();

    void drawEmblemFrame(int highlightColumn, bool paced);
    void drawWordmarkFrame(int highlightColumn, bool paced);
    void drawGrid(const std::vector<std::string>& rows, int highlightColumn,
                  const model::Color& topColor, const model::Color& bottomColor, bool paced);

    static std::string formatElapsed(std::int64_t milliseconds);

    AnimationService& animation_;
    SelfcheckService& selfcheck_;
    SessionStateService& sessionState_;
    const model::BootSettings& settings_;
    utils::Logger& logger_;
    model::SystemFacts facts_;
};

}  // namespace prts::service
