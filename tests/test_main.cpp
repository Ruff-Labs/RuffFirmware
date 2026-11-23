// Runs every TEST(...) in the tests/ folder. Exit code 0 = all passed (what ctest checks).
//   ./build/tests/unit_tests          run everything
//   ./build/tests/unit_tests timer    only tests whose name contains "timer"
#include "test.hpp"
#include <cstring>

int main(int argc, char** argv) {
    const char* filter = argc > 1 ? argv[1] : nullptr;
    int run = 0, failed = 0;
    for (const test::Case& c : test::cases()) {
        if (filter && !std::strstr(c.name, filter)) continue;
        const int before = test::failures();
        std::printf("%s\n", c.name);
        c.fn();
        ++run;
        if (test::failures() != before) ++failed;
    }
    std::printf("\n%d tests, %d failed\n", run, failed);
    return failed ? 1 : 0;
}
