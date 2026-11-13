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

// Composes the PRTS terminal sequence: a monochrome header, the diamond emblem
// (drawn by an expanding ripple), a timestamped self-check log, the boot bar
// and the closing record.
//
// Everything printed here is either a name from the setting or measured data -
// no invented labels. Emphasis comes from brightness and inverse video; colour
// is reserved for the two fault states.
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
    void renderChecks(const std::vector<model::CheckResult>& results, bool animate);
    void renderCheckRow(const model::CheckResult& result, bool animate);
    void renderProgress(double ratio, std::int64_t elapsedMs, bool animate);
    void renderVerdict(const std::vector<model::CheckResult>& results, bool animate);
    void renderRecord(const SessionStateService::State& state, bool animate);
    void renderStatusStrip(const model::SystemFacts& facts);

    std::string paint(const std::string& text, const model::Color& color, bool bold = false) const;

    static std::string statusTag(model::CheckStatus status);
    static std::string formatElapsed(std::int64_t milliseconds);
    static std::string twoDigits(int value);

    AnimationService& animation_;
    SelfcheckService& selfcheck_;
    SessionStateService& sessionState_;
    const model::BootSettings& settings_;
    utils::Logger& logger_;

    model::SystemFacts facts_;
    std::string sessionStamp_;
    int sequenceNumber_{1};
};

}  // namespace prts::service
