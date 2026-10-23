#pragma once
// BatteryPoller (PERI-06). Runs on networkTask right after the handler's own acquisition
// (PERI-07) and pushes what changed into PowerModel. The -1.0f "no reading" sentinel is passed
// through on purpose: the model turns it into percentValid = false, never into 0 %.
#include <cstddef>
#include <cstdint>

#include "model/MutationQueue.h"
#include "model/PowerModel.h"

namespace adapters {

struct BatterySample {
    bool initialized = false;
    bool presenceKnown = false;
    bool connected = false;
    bool charging = false;
    float percent = -1.0f;
    float voltage = -1.0f;
};

class BatteryPollerCore {
public:
    size_t poll(const BatterySample& sample, model::Millis now, model::MutationQueue& queue);
    uint32_t posted() const { return posted_; }
    void reset();

private:
    bool primed_ = false;
    BatterySample last_{};
    uint32_t posted_ = 0;
};

}  // namespace adapters

#ifdef ESP_PLATFORM
class BatteryHandler;

namespace adapters {

class BatteryPoller {
public:
    BatteryPoller(BatteryHandler* battery, model::MutationQueue& queue);
    size_t poll(model::Millis now);

private:
    BatteryHandler* battery_;
    model::MutationQueue& queue_;
    BatteryPollerCore core_;
};

}  // namespace adapters
#endif
