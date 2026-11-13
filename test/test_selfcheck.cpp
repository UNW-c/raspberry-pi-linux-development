#include <string>

#include "service/selfcheck_service.h"
#include "test/fakes.h"
#include "test/test_framework.h"
#include "utils/logger.h"

PRTS_TEST(selfcheck_reports_seven_domains) {
    prts::test::FakeProbe probe;
    prts::test::FakeClock clock;
    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/selfcheck.log");
    prts::service::SelfcheckService service(probe, clock, logger);

    prts::model::SystemFacts facts;
    const auto results = service.run(facts);

    PRTS_CHECK_EQ(results.size(), std::size_t{7});
    PRTS_CHECK_EQ(results.front().id, std::string("kernel"));
    PRTS_CHECK_EQ(results.front().value, std::string("Linux 6.18.50+rpt-rpi-v8"));
    PRTS_CHECK_EQ(results.back().id, std::string("session"));
    PRTS_CHECK_EQ(prts::service::SelfcheckService::countByStatus(results, prts::model::CheckStatus::Ok),
                  static_cast<int>(results.size()));
}

PRTS_TEST(selfcheck_collects_facts) {
    prts::test::FakeProbe probe;
    prts::test::FakeClock clock;
    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/selfcheck.log");
    prts::service::SelfcheckService service(probe, clock, logger);

    prts::model::SystemFacts facts;
    service.run(facts);

    PRTS_CHECK_EQ(facts.hostname, std::string("PRTS"));
    PRTS_CHECK_EQ(facts.cpuCores, 4);
    PRTS_CHECK_EQ(facts.ipv4Address, std::string("192.168.210.239"));
    PRTS_CHECK(facts.diskFreePercent() > 70.0);
}

PRTS_TEST(selfcheck_warns_on_hot_cpu) {
    class HotProbe : public prts::driver::ISystemProbe {
    public:
        void collect(prts::model::ProbeDomain domain, prts::model::SystemFacts& facts) const override {
            prts::test::FakeProbe base;
            base.collect(domain, facts);
            if (domain == prts::model::ProbeDomain::Cpu) {
                facts.cpuTemperatureC = 82.0;
            }
        }
    } probe;

    prts::test::FakeClock clock;
    prts::utils::Logger logger(prts::test::temporaryDirectory() + "/selfcheck.log");
    prts::service::SelfcheckService service(probe, clock, logger);

    prts::model::SystemFacts facts;
    const auto results = service.run(facts);
    PRTS_CHECK_EQ(prts::service::SelfcheckService::countByStatus(results, prts::model::CheckStatus::Fail), 1);
}
