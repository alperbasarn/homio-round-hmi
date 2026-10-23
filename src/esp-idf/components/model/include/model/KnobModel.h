#pragma once
// KnobModel (MDL-28, D5, UIX-11). The V11 knob is a peer holding replicated state: a 0..100
// detent counter, settable haptic config, and a link that can die. The screen is authoritative:
//
//   - a `+N` / `-N` line is a delta: applyDelta() moves the replica and returns the steps for the
//     current context through one accumulator (divisor per context, reset on direction reversal
//     and after 600 ms idle);
//   - a coalesced `setpoint=N` is never a value: burstToDelta() turns it into N - lastN clamped
//     to +-10; while a screen-initiated write is outstanding such bursts are stale and dropped,
//     which is what keeps a MEDIA-era counter out of LIGHT brightness on a mode switch;
//   - onEnter() pushes the mode's value down the write path, and the ack (or a timeout) moves
//     the replica to it.
#include <cstdint>

#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"

namespace model {

// The knob firmware's defaults (HapticController3.ino / FOC.ino). `poles` is detents per
// revolution, the firmware's misnomer kept until section 9 Q32 is answered.
struct HapticConfig {
    int16_t poles = 120;
    float detentStrength = 1.0f;
    float snapPoint = 0.55f;
    float voltageLimit = 3.0f;

    bool operator==(const HapticConfig& other) const {
        return poles == other.poles && detentStrength == other.detentStrength &&
               snapPoint == other.snapPoint && voltageLimit == other.voltageLimit;
    }
    bool operator!=(const HapticConfig& other) const { return !(*this == other); }
};

// What the knob currently drives, which fixes the detents-per-step divisor (UIX-11).
enum class KnobContext : uint8_t {
    Wheel = 0,             // 4 detents per wheel step
    TargetToggle = 1,      // 3 per target toggle
    Volume = 2,            // 1 per volume unit
    ClimateHalfDegree = 3, // 1 per 0.5 C
    SeekFine = 4,          // 1 per seek percent
    SeekCoarse = 5,        // 3 per seek percent (the 10 % digit-key path)
    Count = 6
};

struct KnobState {
    int16_t detentCount = 0;          // the replica of the knob's counter, 0..100
    bool linkAlive = false;
    Millis lastLinkActivityMs = -1;
    HapticConfig haptic;
    KnobContext context = KnobContext::Volume;
    int16_t accumulator = 0;
    int8_t lastDirection = 0;
    Millis lastDetentMs = -1;
    int16_t pendingWrite = -1;        // the value pushed to the knob, until acked or timed out
    Millis writeSentAtMs = -1;        // -1 until the adapter takes the request
    uint32_t droppedBursts = 0;       // stale absolute bursts ignored while a write was pending
    uint32_t clampedBursts = 0;       // bursts whose raw delta exceeded +-10
    uint32_t writesAcked = 0;
    uint32_t writeTimeouts = 0;

    bool operator==(const KnobState& other) const;
    bool operator!=(const KnobState& other) const { return !(*this == other); }
};

class KnobModel : public ModelBase<KnobState, 8> {
public:
    enum class Field : uint8_t {
        Delta = 0,         // value = signed detents (never-drop)
        Burst = 1,         // value = absolute counter from a coalesced setpoint=N (coalesce)
        LinkActivity = 2,  // no operand; stamps the link with the drain time (coalesce)
        WriteAck = 3,      // value = the acked counter (never-drop)
        Poles = 4,         // value (coalesce)
        Strength = 5,      // float (coalesce)
        Snap = 6,          // float (coalesce)
        VoltageLimit = 7,  // float (coalesce)
        Count = 8
    };

    static constexpr int kCountMin = 0;
    static constexpr int kCountMax = 100;
    static constexpr int kBurstClamp = 10;
    static constexpr Millis kIdleResetMs = 600;
    static constexpr Millis kLinkTimeoutMs = 5000;
    static constexpr Millis kWriteAckTimeoutMs = 1000;

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs);
    static ModelUpdate makeFloat(Field field, float value, uint32_t atMs);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    // Inbound motion.
    int applyDelta(int detents, Millis now);        // returns steps for the current context
    int burstToDelta(int absoluteCount, Millis now); // returns the clamped detent delta (0 if stale)
    int stepsFor(int detents, Millis now);          // the accumulator alone
    static int divisorFor(KnobContext context);

    // Context and the write path.
    void setContext(KnobContext context);           // resets the accumulator
    void onEnter(KnobContext context, int modeValue, Millis now);
    bool requestWrite(int value, Millis now);
    bool hasPendingWrite() const { return state().pendingWrite >= 0; }
    int takeWriteRequest(Millis now);               // the adapter sends setpoint=N; -1 when none
    bool noteWriteAck(int value);
    void tick(Millis now);                          // write timeout and link liveness

    // Liveness and haptics.
    void noteLinkActivity(Millis now);
    bool linkAlive(Millis now) const;
    bool setHaptic(const HapticConfig& config);
    bool setPoles(int poles);
    bool setDetentStrength(float strength);
    bool setSnapPoint(float snap);
    bool setVoltageLimit(float volts);

private:
    static int clampCount(int value);
    void resetAccumulator();
};

}  // namespace model
