#pragma once
// Single-header test runner (BLD-06). One executable per test_*.cpp; cases register themselves
// through TEST_CASE; CHECK records a failure and continues, REQUIRE records and leaves the case.
#include <cstdio>
#include <vector>

namespace testing {

struct Case {
    const char* name;
    void (*run)();
};

std::vector<Case>& registry();
int& failures();
void fail(const char* file, int line, const char* what);

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back(Case{name, run}); }
};

}  // namespace testing

#define TEST_CASE(name)                                            \
    static void name();                                            \
    static ::testing::Registrar name##_registrar(#name, &name);    \
    static void name()

#define CHECK(expr)                                                              \
    do {                                                                         \
        if (!(expr)) ::testing::fail(__FILE__, __LINE__, "CHECK(" #expr ")");    \
    } while (0)

#define CHECK_EQ(a, b)                                                                  \
    do {                                                                                \
        if (!((a) == (b))) ::testing::fail(__FILE__, __LINE__, "CHECK_EQ(" #a ", " #b ")"); \
    } while (0)

#define REQUIRE(expr)                                                        \
    do {                                                                     \
        if (!(expr)) {                                                       \
            ::testing::fail(__FILE__, __LINE__, "REQUIRE(" #expr ")");       \
            return;                                                          \
        }                                                                    \
    } while (0)
