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

// Composes the PRTS terminal sequence: a gold-ruled header, the diamond
// emblem, seven numbered self-check rows, the boot bar and the closing record.
//
// Visual language: monochrome first (white and greys), one warm gold for the
// active elements, hairline rules, numbered rows, and the diamond (Originium)
// motif. Colour never carries meaning on its own - the status word does.
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
    bool useFullLayout() const;
    int contentWidth() const;

    void renderHeader();
    void renderRule();
    void renderIdentity(bool animate);
    void renderCompactIdentity(bool animate);
    void renderChecks(const std::vector<model::CheckResult>& results, std::int64_t startedAt,
                      bool animate);
    void renderCheckRow(const model::CheckResult& result, int number, bool animate);
    void renderProgress(double ratio, std::int64_t elapsedMs, bool animate);
    void renderVerdict(const std::vector<model::CheckResult>& results, bool animate);
    void renderWelcome(const SessionStateService::State& state, bool animate);
    void renderStatusStrip(const model::SystemFacts& facts);

    std::string paint(const std::string& text, const model::Color& color, bool bold = false) const;
    std::string padBetween(const std::string& left, const std::string& right) const;

    static std::string statusTag(model::CheckStatus status);
    static std::string formatElapsed(std::int64_t milliseconds);
    static std::string twoDigits(int value);

    AnimationService& animation_;
    SelfcheckService& selfcheck_;
    SessionStateService& sessionState_;
    const model::BootSettings& settings_;
    utils::Logger& logger_;

    model::SystemFacts facts_;
    int sequenceNumber_{1};
};

}  // namespace prts::service
