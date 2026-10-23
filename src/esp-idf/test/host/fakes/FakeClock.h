#pragma once
#include "model/Clock.h"

namespace fakes {

// A clock that only moves when the test says so.
struct FakeClock {
    model::Millis now = 0;

    void advance(model::Millis ms) { now += ms; }

    model::Clock clock() { return model::Clock{&FakeClock::read, this}; }

    static model::Millis read(void* context) { return static_cast<FakeClock*>(context)->now; }
};

}  // namespace fakes
