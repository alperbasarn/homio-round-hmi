#pragma once
// LightModel (MDL-16, reshaped by LED-02): the control state of the strip the LIGHT screen drives,
// in HMI units: brightness 0..100, colour as 0xRRGGBB with the ring hue beside it, one of the eight
// wire effects, its five parameters and its colour. Local changes (knob, touch, a console command)
// mark fields pending for the bridge, which publishes one `set` per 100 ms window with a trailing
// flush and a fresh seq (takePending). What the strip reports back arrives through adoptRemote():
// an echo of our own seq is applied silently, a stale echo is dropped, a remote source (web page,
// Matter, another HMI) wins and is applied silently too, so nothing ever ping-pongs. Brightness is
// 0..255 on the wire; the single conversion lives here (NET-22).
#include <cstdint>

#include "model/LedRegistryModel.h"
#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"
#include "model/RateLimit.h"

namespace model {

struct LightState {
    bool power = true;
    Percent brightness = 50;   // the old default waterLevel
    float hue = 0.5f;          // the ring position 0..1; rgb follows a local hue, hue a remote rgb
    uint32_t rgb = 0x00FFFFu;  // hsvToRgb(0.5, 1, 1): the old default swatch
    LedEffect effect = LedEffect::Solid;
    uint8_t params[kLedParamCount] = {0, 0, 0, 0, 0};
    uint32_t effectRgb = 0xFFFFFFu;
    int8_t stripSlot = -1;     // the registry slot this state controls; -1 = no strip selected
    bool operator==(const LightState& other) const;
    bool operator!=(const LightState& other) const { return !(*this == other); }
};

class LightModel : public ModelBase<LightState, 8> {
public:
    enum class Field : uint8_t {
        Brightness = 0,       // value = percent 0..100 (coalesce)
        BrightnessWire = 1,   // value = 0..255 as received on the wire (coalesce)
        Hue = 2,              // float 0..1 (coalesce)
        Power = 3,            // bool (never-drop)
        Effect = 4,           // value = LedEffect (never-drop)
        EffectStep = 5,       // value = signed steps around the eight (never-drop)
        Color = 6,            // value = 0xRRGGBB (coalesce)
        Params = 7,           // value = params 0..3 packed little-endian, aux = param 4 (coalesce)
        EffectColor = 8,      // value = 0xRRGGBB (coalesce)
        BrightnessDelta = 9,  // value = signed detents, D5-clamped upstream (never-drop)
        Count = 10
    };
    // Which fields changed locally since the bridge last took them.
    enum Pending : uint16_t {
        PendingPower = 1u << 0,
        PendingBrightness = 1u << 1,
        PendingColor = 1u << 2,
        PendingEffect = 1u << 3,
        PendingParams = 1u << 4,
        PendingEffectColor = 1u << 5
    };

    static constexpr Millis kPublishIntervalMs = 100;  // LED-02: the MDL-12 light window
    static constexpr int kWireMax = 255;

    LightModel();
    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs, int32_t aux = 0);
    static ModelUpdate makeFloat(Field field, float value, uint32_t atMs);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    // Local control: every setter that changes something marks its field pending.
    bool setPower(bool on);
    bool setBrightness(int percent);       // 0..100 only
    bool setBrightnessFromWire(int wire);  // 0..255 only
    bool adjustBrightness(int delta);      // clamps; false when nothing moved
    bool setHue(float hue);                // clamped to 0..1, NaN rejected; rgb follows
    bool setColor(uint32_t rgb);           // hue follows
    bool setEffect(LedEffect effect);
    LedEffect stepEffect(int steps);       // wraps around the eight
    bool setParams(const uint8_t params[kLedParamCount]);
    bool setEffectColor(uint32_t rgb);

    // The strip this state controls. Switching adopts the reported state (when known) silently
    // and discards anything pending for the previous strip.
    bool follow(int slot, const LedStripState* reported);
    // A `state` from the strip. Source Self with the current seq is the final echo: applied except
    // where a newer local change is still pending. Source Self with an older seq is stale only
    // while a newer set is pending or in flight (not yet echoed); once everything is acknowledged
    // it is the strip telling the truth (a replay after a reconnect) and is applied. Any other
    // source is a remote change, applied in full, and it cancels whatever was pending. Never marks a field pending. Returns true when the local state changed.
    bool adoptRemote(const LedStripState& s, LedSource source, uint32_t seq);

    // Bridge side. takePending() clears the pending fields and returns the seq for that publish.
    uint16_t pendingFields() const { return pending_; }
    uint32_t sentSeq() const { return sentSeq_; }
    uint32_t takePending(uint16_t& fields);
    // The publish of a taken set failed: the fields are pending again and the window is re-armed.
    void restorePending(uint16_t fields);
    uint32_t ackedSeq() const { return ackedSeq_; }
    // Drops pending changes and resets the window: a command the strip may not receive (offline,
    // not mine) is refused locally, never queued.
    void discardPending() { pending_ = 0; publish_.reset(); }
    LedStripState asStripState() const;  // what a full `set` would carry (count is 0)

    void markChanged() { publish_.markChanged(); }
    bool shouldPublish(Millis now) { return publish_.shouldPublish(now); }
    bool flushPublish() { return publish_.flush(); }
    bool publishPending() const { return publish_.pending(); }

    // The single wire-scale rule (NET-22): 0/50/100 -> 0/127/255 and back.
    static int wireFromPercent(int percent);
    static int percentFromWire(int wire);
    uint32_t publishedRgb() const;  // brightness folded into value, what the old ring received
    uint32_t swatchRgb() const { return state().rgb; }

private:
    void markLocal(uint16_t bit);
    RateLimiter publish_;
    uint16_t pending_ = 0;
    uint32_t sentSeq_ = 0;
    uint32_t ackedSeq_ = 0;  // the last own seq the strip echoed
};

}  // namespace model
