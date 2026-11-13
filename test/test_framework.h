#pragma once

#include <functional>
#include <string>
#include <vector>

namespace prts::test {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();
int runAll();
void reportFailure(const std::string& message);

struct Registrar {
    Registrar(std::string name, std::function<void()> body);
};

}  // namespace prts::test

#define PRTS_TEST(name)                                                  \
    static void name();                                                  \
    static ::prts::test::Registrar name##_registrar(#name, name);        \
    static void name()

#define PRTS_CHECK(condition)                                                          \
    do {                                                                               \
        if (!(condition)) {                                                            \
            ::prts::test::reportFailure(std::string("CHECK failed: ") + #condition +   \
                                        " at " __FILE__ ":" + std::to_string(__LINE__)); \
        }                                                                              \
    } while (false)

#define PRTS_CHECK_EQ(actual, expected)                                                 \
    do {                                                                                \
        const auto actualValue = (actual);                                              \
        const auto expectedValue = (expected);                                          \
        if (!(actualValue == expectedValue)) {                                          \
            ::prts::test::reportFailure(std::string("CHECK_EQ failed: ") + #actual +     \
                                        " == " + #expected + " at " __FILE__ ":" +      \
                                        std::to_string(__LINE__));                      \
        }                                                                               \
    } while (false)
