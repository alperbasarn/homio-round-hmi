#pragma once
// Test-only allocation counter (MDL-06). The global operator new/delete are replaced in
// HeapGuard.cpp; NoHeapScope records a failure if anything allocated between its construction
// and destruction, which is how "no heap in the render path" becomes an assertion.
#include <cstddef>

namespace testing {

size_t allocationCount();

class NoHeapScope {
public:
    NoHeapScope(const char* file, int line);
    ~NoHeapScope();

    NoHeapScope(const NoHeapScope&) = delete;
    NoHeapScope& operator=(const NoHeapScope&) = delete;

private:
    size_t start_;
    const char* file_;
    int line_;
};

}  // namespace testing

#define NO_HEAP_SCOPE() ::testing::NoHeapScope noHeapScopeGuard(__FILE__, __LINE__)
