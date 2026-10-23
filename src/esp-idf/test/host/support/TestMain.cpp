#include "TestRunner.h"

#include <cstdio>

namespace testing {

std::vector<Case>& registry() {
    static std::vector<Case> cases;
    return cases;
}

int& failures() {
    static int count = 0;
    return count;
}

void fail(const char* file, int line, const char* what) {
    ++failures();
    std::printf("  FAIL %s:%d: %s\n", file, line, what);
}

}  // namespace testing

int main() {
    int failedCases = 0;
    for (const testing::Case& testCase : testing::registry()) {
        const int before = testing::failures();
        testCase.run();
        const bool ok = testing::failures() == before;
        if (!ok) {
            ++failedCases;
        }
        std::printf("[%s] %s\n", ok ? " ok " : "FAIL", testCase.name);
    }
    std::printf("%zu case(s), %d failed, %d check failure(s)\n",
                testing::registry().size(), failedCases, testing::failures());
    return failedCases == 0 ? 0 : 1;
}
