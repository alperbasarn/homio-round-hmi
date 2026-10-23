#include "adapters/BatteryPoller.h"

namespace adapters {

using Field = model::PowerModel::Field;

void BatteryPollerCore::reset() {
    primed_ = false;
    last_ = BatterySample{};
    posted_ = 0;
}

size_t BatteryPollerCore::poll(const BatterySample& sample, model::Millis now, model::MutationQueue& queue) {
    if (!sample.initialized) {
        return 0;
    }
    const uint32_t atMs = static_cast<uint32_t>(now);
    size_t count = 0;

    if (!primed_ || sample.presenceKnown != last_.presenceKnown) {
        queue.post(model::PowerModel::make(Field::PresenceKnown, sample.presenceKnown ? 1 : 0, atMs));
        ++count;
    }
    if (!primed_ || sample.connected != last_.connected) {
        queue.post(model::PowerModel::make(Field::Connected, sample.connected ? 1 : 0, atMs));
        ++count;
    }
    if (!primed_ || sample.charging != last_.charging) {
        queue.post(model::PowerModel::make(Field::Charging, sample.charging ? 1 : 0, atMs));
        ++count;
    }
    if (!primed_ || sample.percent != last_.percent) {
        queue.post(model::PowerModel::makeFloat(Field::Percent, sample.percent, atMs));
        ++count;
    }
    if (!primed_ || sample.voltage != last_.voltage) {
        queue.post(model::PowerModel::makeFloat(Field::Voltage, sample.voltage, atMs));
        ++count;
    }

    primed_ = true;
    last_ = sample;
    posted_ += static_cast<uint32_t>(count);
    return count;
}

}  // namespace adapters

#ifdef ESP_PLATFORM
#include "BatteryHandler.h"

namespace adapters {

BatteryPoller::BatteryPoller(BatteryHandler* battery, model::MutationQueue& queue)
    : battery_(battery), queue_(queue), core_() {}

size_t BatteryPoller::poll(model::Millis now) {
    BatterySample sample;
    if (battery_ != nullptr && battery_->isInitialized()) {
        const BatteryHandler::BatteryTelemetry telemetry = battery_->getBatteryTelemetry();
        sample.initialized = true;
        sample.presenceKnown = battery_->isPresenceKnown();
        sample.connected = battery_->isBatteryConnected();
        sample.charging = battery_->isCharging();
        sample.percent = telemetry.percentage;
        sample.voltage = telemetry.voltageVolts;
    }
    return core_.poll(sample, now, queue_);
}

}  // namespace adapters
#endif
