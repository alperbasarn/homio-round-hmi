#include "model/TraceHooks.h"

#include <atomic>

namespace model {
namespace trace {

namespace {

Hooks g_hooks;
std::atomic<bool> g_enabled{false};
std::atomic<uint32_t> g_writes{0};
std::atomic<uint32_t> g_offTask{0};

}  // namespace

void install(const Hooks& hooks) {
    g_hooks = hooks;
    g_writes.store(0, std::memory_order_relaxed);
    g_offTask.store(0, std::memory_order_relaxed);
    g_enabled.store(true, std::memory_order_release);
}

void clear() {
    g_enabled.store(false, std::memory_order_release);
    g_hooks = Hooks{};
    g_writes.store(0, std::memory_order_relaxed);
    g_offTask.store(0, std::memory_order_relaxed);
}

bool enabled() {
    return g_enabled.load(std::memory_order_acquire);
}

void noteWrite() {
    if (!enabled()) {
        return;
    }
    g_writes.fetch_add(1, std::memory_order_relaxed);
    if (g_hooks.onConsumerTask != nullptr && !g_hooks.onConsumerTask(g_hooks.guardContext)) {
        g_offTask.fetch_add(1, std::memory_order_relaxed);
    }
}

uint32_t writes() {
    return g_writes.load(std::memory_order_relaxed);
}

uint32_t offTaskWrites() {
    return g_offTask.load(std::memory_order_relaxed);
}

void resetCounters() {
    g_writes.store(0, std::memory_order_relaxed);
    g_offTask.store(0, std::memory_order_relaxed);
}

const char* currentTaskName() {
    if (!enabled() || g_hooks.taskName == nullptr) {
        return "?";
    }
    const char* name = g_hooks.taskName(g_hooks.nameContext);
    return name != nullptr ? name : "?";
}

void log(const char* line) {
    if (enabled() && g_hooks.log != nullptr && line != nullptr) {
        g_hooks.log(line, g_hooks.logContext);
    }
}

}  // namespace trace
}  // namespace model
