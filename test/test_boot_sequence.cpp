#include <string>

#include "model/boot_settings.h"
#include "service/animation_service.h"
#include "service/boot_sequence_service.h"
#include "service/selfcheck_service.h"
#include "service/session_state_service.h"
#include "test/fakes.h"
#include "test/test_framework.h"
#include "utils/logger.h"

namespace {

struct Fixture {
    Fixture()
        : session(prts::test::temporaryDirectory() + "/session-" +
                  std::to_string(nextSessionId()) + ".state") {}

    static int nextSessionId() {
        static int counter = 0;
        return ++counter;
    }

    prts::test::FakeTerminal terminal;
    prts::test::FakeClock clock;
    prts::test::FakeProbe probe;
    prts::model::BootSettings settings;
    prts::utils::Logger logger{prts::test::temporaryDirectory() + "/boot.log",
                               prts::utils::LogLevel::Error};
    prts::service::AnimationService animation{terminal, clock, settings, logger};
    prts::service::SelfcheckService selfcheck{probe, clock, logger};
    prts::service::SessionStateService session;
    prts::service::BootSequenceService boot{animation, selfcheck, session, settings, logger};
};

}  // namespace

PRTS_TEST(boot_sequence_full_layout) {
    Fixture fixture;
    fixture.terminal.size_ = {100, 40};

    const auto outcome = fixture.boot.run(false);

    PRTS_CHECK_EQ(outcome.okCount, 7);
    PRTS_CHECK_EQ(outcome.warnCount, 0);
    PRTS_CHECK_EQ(outcome.failCount, 0);
    PRTS_CHECK_EQ(fixture.terminal.lineCount(), 28);
    PRTS_CHECK(fixture.terminal.contains("PRTS"));
    PRTS_CHECK(fixture.terminal.contains("PRIMARY RHODES ISLAND TERMINAL SYSTEM"));
    PRTS_CHECK(fixture.terminal.contains("LINK: ACTIVE"));
    PRTS_CHECK(fixture.terminal.contains("KERNEL"));
    PRTS_CHECK(fixture.terminal.contains("[ OK ]"));
    PRTS_CHECK(fixture.terminal.contains("BOOT SEQUENCE"));
    PRTS_CHECK(fixture.terminal.contains("ALL SYSTEMS NOMINAL"));
}

PRTS_TEST(boot_sequence_compact_layout) {
    Fixture fixture;
    fixture.terminal.size_ = {100, 24};

    fixture.boot.run(false);

    PRTS_CHECK_EQ(fixture.terminal.lineCount(), 16);
    PRTS_CHECK(fixture.terminal.contains("\u25c6 PRTS \u25c6"));
    PRTS_CHECK(fixture.terminal.contains("MEM"));
}

PRTS_TEST(boot_sequence_records_session) {
    Fixture fixture;
    fixture.terminal.size_ = {100, 40};

    fixture.boot.run(false);
    const auto first = fixture.session.read();
    PRTS_CHECK_EQ(first.loginCount, std::uint64_t{1});
    PRTS_CHECK_EQ(first.lastLogin, std::string("2026-01-01 08:00:00"));

    fixture.boot.run(false);
    const auto second = fixture.session.read();
    PRTS_CHECK_EQ(second.loginCount, std::uint64_t{2});
    PRTS_CHECK(fixture.terminal.contains("\u7b2c 2 \u6b21\u63a5\u5165"));
}

PRTS_TEST(boot_sequence_fits_terminal_width) {
    Fixture fixture;
    fixture.terminal.size_ = {80, 30};

    fixture.boot.run(false);
    PRTS_CHECK_EQ(fixture.terminal.lineCount(), 28);
}
