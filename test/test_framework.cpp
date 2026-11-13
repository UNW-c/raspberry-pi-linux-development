#include "test/test_framework.h"

#include <cstdio>
#include <string>
#include <utility>

namespace prts::test {
namespace {

int g_failures = 0;
int g_currentFailures = 0;

}  // namespace

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar(std::string name, std::function<void()> body) {
    registry().push_back(TestCase{std::move(name), std::move(body)});
}

void reportFailure(const std::string& message) {
    ++g_currentFailures;
    std::fprintf(stderr, "  FAIL: %s\n", message.c_str());
}

int runAll() {
    int failedTests = 0;
    for (const TestCase& test : registry()) {
        g_currentFailures = 0;
        std::printf("[ RUN      ] %s\n", test.name.c_str());
        test.body();
        if (g_currentFailures == 0) {
            std::printf("[       OK ] %s\n", test.name.c_str());
        } else {
            std::printf("[  FAILED  ] %s (%d failed checks)\n", test.name.c_str(), g_currentFailures);
            ++failedTests;
            g_failures += g_currentFailures;
        }
    }
    std::printf("\n%d test(s) run, %d failed, %d failed check(s)\n",
                static_cast<int>(registry().size()), failedTests, g_failures);
    return failedTests == 0 ? 0 : 1;
}

}  // namespace prts::test
