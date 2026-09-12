#pragma once
// ModelTrace, the hook half (MDL-32). Compile-gated by QNOB_MODEL_TRACE (set by the build from
// the Kconfig switch of the same name, off by default) at the two call sites: ModelBase::mutate() and the
// AppModel drain. This header has no platform includes; the composition installs the hooks.
//
// The off-task write assertion: every model write passes through mutate(), which calls
// noteWrite(); when the installed guard says the caller is not the consumer task the write is
// counted in offTaskWrites(). The P1b gate reads exactly 0.
#include <cstddef>
#include <cstdint>

namespace model {
namespace trace {

using LogFn = void (*)(const char* line, void* context);
using GuardFn = bool (*)(void* context);            // true when running on the consumer task
using TaskNameFn = const char* (*)(void* context);  // the current task's name, for the drain line

struct Hooks {
    LogFn log = nullptr;
    void* logContext = nullptr;
    GuardFn onConsumerTask = nullptr;
    void* guardContext = nullptr;
    TaskNameFn taskName = nullptr;
    void* nameContext = nullptr;
};

void install(const Hooks& hooks);  // enables tracing
void clear();                      // disables and resets the counters
bool enabled();

void noteWrite();                  // called from ModelBase::mutate() when compiled in
uint32_t writes();
uint32_t offTaskWrites();
void resetCounters();

const char* currentTaskName();     // "?" without a hook
void log(const char* line);        // no-op without a hook

}  // namespace trace
}  // namespace model
