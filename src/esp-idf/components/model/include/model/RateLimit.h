#pragma once
// Timing policies with no clock of their own (MDL-12): every method takes nowMs, so the tests
// drive them with FakeClock and the render path never reads a timer through them.
#include <cstdint>

#include "model/ModelTypes.h"

namespace model {

// At most one publish per window, with the last value never lost: a change marks the limiter
// pending; shouldPublish() answers true once a window has elapsed since the previous publish;
// flush() emits a pending value regardless of the window. The first publish is never delayed.
class RateLimiter {
public:
    explicit RateLimiter(Millis intervalMs);

    void markChanged() { pending_ = true; }
    bool shouldPublish(Millis nowMs);
    bool flush();

    bool pending() const { return pending_; }
    Millis intervalMs() const { return interval_; }
    Millis lastPublishMs() const { return lastPublish_; }
    void reset();

private:
    Millis interval_;
    Millis lastPublish_;
    bool pending_;
    bool everPublished_;
};

// Fires once after the input has been quiet for settleMs (the climate target: publish the value
// the knob settled on, not every detent).
class Debouncer {
public:
    explicit Debouncer(Millis settleMs);

    void poke(Millis nowMs);        // activity: restart the quiet period
    bool settled(Millis nowMs);     // true exactly once per burst, after the quiet period
    bool armed() const { return armed_; }
    Millis settleMs() const { return settle_; }

private:
    Millis settle_;
    Millis lastPoke_;
    bool armed_;
};

// Remembers what was just published so the broker's echo of it is not mistaken for an external
// change. An inbound value equal to the last published one within the window is an echo.
class EchoSuppressor {
public:
    explicit EchoSuppressor(Millis windowMs);

    void published(int32_t value, Millis nowMs);
    bool isEcho(int32_t value, Millis nowMs) const;
    void clear();
    Millis windowMs() const { return window_; }

private:
    Millis window_;
    Millis publishedAt_;
    int32_t value_;
    bool valid_;
};

}  // namespace model
