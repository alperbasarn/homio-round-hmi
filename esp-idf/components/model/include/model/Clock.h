#pragma once
#include "model/ModelTypes.h"

namespace model {

// Injectable monotonic clock (MDL-02). The composition points it at the device's timer; tests
// use FakeClock. Model code never reads a clock on its own: policies take nowMs as a
// parameter, and the aggregate root holds one of these for the drain.
struct Clock {
    using NowFn = Millis (*)(void* context);

    NowFn now = nullptr;
    void* context = nullptr;

    Millis nowMs() const { return now ? now(context) : 0; }
    bool valid() const { return now != nullptr; }
};

}  // namespace model
