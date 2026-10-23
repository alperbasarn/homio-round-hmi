#include "adapters/EnvironmentPoller.h"

namespace adapters {

using Field = model::EnvironmentModel::Field;

void EnvironmentPollerCore::reset() {
    lastLocalMinute_ = -1;
    lastOffset_ = 0;
    tempPrimed_ = false;
    lastTemp_ = 0.0f;
    posted_ = 0;
}

size_t EnvironmentPollerCore::poll(const EnvironmentSample& sample, model::Millis now, model::MutationQueue& queue) {
    const uint32_t atMs = static_cast<uint32_t>(now);
    size_t count = 0;

    if (sample.clockValid) {
        const int64_t local = sample.epochSeconds + static_cast<int64_t>(sample.utcOffsetMinutes) * 60;
        const int64_t minute = local / 60;
        if (minute != lastLocalMinute_ || sample.utcOffsetMinutes != lastOffset_) {
            // The epoch travels as an int32 (good until 2038); the model derives the civil date.
            queue.post(model::EnvironmentModel::make(Field::Epoch, static_cast<int32_t>(sample.epochSeconds), atMs,
                                                     sample.utcOffsetMinutes));
            ++count;
            lastLocalMinute_ = minute;
            lastOffset_ = sample.utcOffsetMinutes;
        }
    }

    if (sample.outdoorValid && (!tempPrimed_ || sample.outdoorTempC != lastTemp_)) {
        queue.post(model::EnvironmentModel::makeFloat(Field::OutdoorTemp, sample.outdoorTempC, atMs));
        ++count;
        tempPrimed_ = true;
        lastTemp_ = sample.outdoorTempC;
    }

    posted_ += static_cast<uint32_t>(count);
    return count;
}

}  // namespace adapters

#ifdef ESP_PLATFORM
#include "TimeHandler.h"
#include "WeatherHandler.h"

namespace adapters {

EnvironmentPoller::EnvironmentPoller(TimeHandler* time, WeatherHandler* weather, model::MutationQueue& queue)
    : time_(time), weather_(weather), queue_(queue), core_() {}

size_t EnvironmentPoller::poll(model::Millis now) {
    EnvironmentSample sample;
    if (time_ != nullptr && time_->isTimeInitialized()) {
        sample.clockValid = true;
        sample.epochSeconds = static_cast<int64_t>(time_->epochNow());
        sample.utcOffsetMinutes = time_->utcOffsetMinutes();
    }
    if (weather_ != nullptr && weather_->hasReading()) {
        sample.outdoorValid = true;
        sample.outdoorTempC = weather_->getTemperature();
    }
    return core_.poll(sample, now, queue_);
}

}  // namespace adapters
#endif
