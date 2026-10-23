#pragma once
// EnvironmentPoller (NET-07). Runs on networkTask; posts the clock as an epoch plus the UTC
// offset, never as formatted strings, and only when the local minute changes; posts the outdoor
// temperature when the weather handler has a new reading. The INFO screen keeps its own 1 Hz
// colon blink; the model only changes once a minute.
#include <cstddef>
#include <cstdint>

#include "model/EnvironmentModel.h"
#include "model/MutationQueue.h"

namespace adapters {

struct EnvironmentSample {
    bool clockValid = false;
    int64_t epochSeconds = 0;
    int utcOffsetMinutes = 0;
    bool outdoorValid = false;
    float outdoorTempC = 0.0f;
};

class EnvironmentPollerCore {
public:
    size_t poll(const EnvironmentSample& sample, model::Millis now, model::MutationQueue& queue);
    uint32_t posted() const { return posted_; }
    void reset();

private:
    int64_t lastLocalMinute_ = -1;
    int lastOffset_ = 0;
    bool tempPrimed_ = false;
    float lastTemp_ = 0.0f;
    uint32_t posted_ = 0;
};

}  // namespace adapters

#ifdef ESP_PLATFORM
class TimeHandler;
class WeatherHandler;

namespace adapters {

class EnvironmentPoller {
public:
    EnvironmentPoller(TimeHandler* time, WeatherHandler* weather, model::MutationQueue& queue);
    size_t poll(model::Millis now);

private:
    TimeHandler* time_;
    WeatherHandler* weather_;
    model::MutationQueue& queue_;
    EnvironmentPollerCore core_;
};

}  // namespace adapters
#endif
