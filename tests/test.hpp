#pragma once
// Tiny test runner, so the tests build anywhere without downloading a framework.
//
//   TEST(name) { CHECK(a == b); CHECK_NEAR(x, 1.5, 0.001); }
//
// Every TEST in every file registers itself; test_main.cpp runs them all. A failed CHECK
// prints file:line and the expression, and the test keeps going (so one run shows every failure).
#include <cmath>
#include <cstdio>
#include <vector>

namespace test {

struct Case {
    const char* name;
    void (*fn)();
};

inline std::vector<Case>& cases() {
    static std::vector<Case> all;
    return all;
}

inline int& failures() {
    static int n = 0;
    return n;
}

struct Register {
    Register(const char* name, void (*fn)()) { cases().push_back({ name, fn }); }
};

}  // namespace test

#define TEST(name)                                         \
    static void name();                                    \
    static test::Register register_##name(#name, name);    \
    static void name()

#define CHECK(cond)                                                              \
    do {                                                                         \
        if (!(cond)) {                                                           \
            std::printf("    FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            ++test::failures();                                                  \
        }                                                                        \
    } while (0)

#define CHECK_NEAR(a, b, tol)                                                    \
    do {                                                                         \
        const double a_ = (a), b_ = (b);                                         \
        if (!(std::fabs(a_ - b_) <= (tol))) {                                    \
            std::printf("    FAIL %s:%d: %s = %g, expected %g\n",                \
                        __FILE__, __LINE__, #a, a_, b_);                         \
            ++test::failures();                                                  \
        }                                                                        \
    } while (0)
