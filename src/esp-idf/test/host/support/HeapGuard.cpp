#include "HeapGuard.h"

#include <atomic>
#include <cstdlib>
#include <new>

#include "TestRunner.h"

namespace {

std::atomic<size_t> g_allocations{0};

void* countedAllocate(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    void* memory = std::malloc(size == 0 ? 1 : size);
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

}  // namespace

void* operator new(std::size_t size) { return countedAllocate(size); }
void* operator new[](std::size_t size) { return countedAllocate(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace testing {

size_t allocationCount() {
    return g_allocations.load(std::memory_order_relaxed);
}

NoHeapScope::NoHeapScope(const char* file, int line) : start_(allocationCount()), file_(file), line_(line) {}

NoHeapScope::~NoHeapScope() {
    const size_t delta = allocationCount() - start_;
    if (delta != 0) {
        fail(file_, line_, "heap allocation inside NoHeapScope");
    }
}

}  // namespace testing
