#pragma once
// LightModel (MDL-16). Brightness is 0..100 everywhere inside the device; the single wire-scale
// conversion to the LED ring's 0..255 lives here (NET-22) so no controller repeats it.
// LightController's `setpoint` (dead, read once) does not exist here; `waterLevel` is
// `brightness`.
#include <cstdint>

#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"
#include "model/RateLimit.h"

namespace model {

enum class LedMode : uint8_t { Static = 0, Glow = 1, Rainbow = 2, Count = 3 };

struct LightState {
    Percent brightness = 50;   // LightController's default waterLevel
    float hue = 0.5f;          // 0..1, the old colorHue default
    float saturation = 1.0f;   // 0..1; both call sites use 1.0 today
    LedMode mode = LedMode::Static;

    bool operator==(const LightState& other) const {
        return brightness == other.brightness && hue == other.hue && saturation == other.saturation &&
               mode == other.mode;
    }
    bool operator!=(const LightState& other) const { return !(*this == other); }
};

class LightModel : public ModelBase<LightState, 8> {
public:
    enum class Field : uint8_t {
        Brightness = 0,      // value = percent 0..100 (coalesce)
        BrightnessWire = 1,  // value = 0..255 as received on the wire (coalesce)
        Hue = 2,             // float (coalesce)
        Saturation = 3,      // float (coalesce)
        Mode = 4,            // value = LedMode (never-drop)
        Count = 5
    };

    static constexpr Millis kPublishIntervalMs = 25;  // CTRL-04's measured slot
    static constexpr int kWireMax = 255;

    LightModel();

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs);
    static ModelUpdate makeFloat(Field field, float value, uint32_t atMs);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    bool setBrightness(int percent);          // 0..100 only
    bool setBrightnessFromWire(int wire);     // 0..255 only
    bool adjustBrightness(int delta);         // clamps; false when nothing moved
    bool setHue(float hue);                   // clamped to 0..1; NaN rejected
    bool setSaturation(float saturation);     // clamped to 0..1; NaN rejected
    bool setMode(LedMode mode);
    LedMode cycleMode();                      // Static -> Glow -> Rainbow -> Static

    // The single wire-scale rule (NET-22): 0/50/100 -> 0/127/255 and back.
    static int wireFromPercent(int percent);
    static int percentFromWire(int wire);

    uint32_t publishedRgb() const;  // brightness folded into value, what the ring receives
    uint32_t swatchRgb() const;     // full value, what the screen draws

    void markChanged() { publish_.markChanged(); }
    bool shouldPublish(Millis now) { return publish_.shouldPublish(now); }
    bool flushPublish() { return publish_.flush(); }
    bool publishPending() const { return publish_.pending(); }

private:
    RateLimiter publish_;
};

}  // namespace model
