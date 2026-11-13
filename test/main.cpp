#include <cstdio>

#include "test/fakes.h"
#include "test/test_framework.h"

int main() {
    std::printf("PRTS unit tests\n\n");
    const int result = prts::test::runAll();
    prts::test::clearTemporaryDirectory();
    return result;
}
