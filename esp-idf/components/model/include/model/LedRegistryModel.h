#pragma once
// LedRegistryModel (LED-01, dev_notes/led_mqtt_contract.md): every LED strip the broker has shown
// this HMI, in eight fixed slots, plus the one the LIGHT screen drives. A slot is claimed by an
// announce (`info`), refreshed by every message, set online or offline by `status` and by the
// presence timeout, and "mine" when the strip lists this HMI among its controllers (or the
// persisted pairing list says so at boot). Strings reach the model through the display-task
// setters (the bridge drains its inbound frames there, as the media path does); every numeric
// change also has a record, and the field byte carries the slot so two strips never coalesce
// into one another. No heap anywhere: fixed strings, fixed arrays.
#include <cstddef>
#include <cstdint>

#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"

namespace model {

// The eight effects the strip firmware names on the wire. Solid, Glow and Rainbow keep the old
// LedMode numbering (0, 1, 2), so the current LIGHT screen cycle still lands on them.
enum class LedEffect : uint8_t {
    Solid = 0,
    Glow = 1,
    Rainbow = 2,
    Chase = 3,
    Sparkle = 4,
    Wave = 5,
    Fire = 6,
    Aurora = 7,
    Count = 8
};
const char* ledEffectName(LedEffect effect);
bool ledEffectFromName(const char* name, LedEffect& out);
LedEffect stepLedEffect(LedEffect effect, int steps);  // wraps around the eight

// Who changed a strip last: the `src` of its `state`, as seen from this HMI.
enum class LedSource : uint8_t {
    Unknown = 0,
    Self = 1,             // this HMI (its own controller id)
    OtherController = 2,  // another HMI
    Web = 3,
    Matter = 4,
    Schedule = 5,
    Device = 6,           // the retained publish on connect
    Count = 7
};
const char* ledSourceName(LedSource source);

constexpr size_t kLedIdCapacity = 16;
constexpr size_t kLedNameCapacity = 24;
constexpr size_t kLedParamCount = 5;

// The control tuple as the strip reports it, in HMI units (brightness 0..100).
struct LedStripState {
    bool power = false;
    Percent brightness = 0;
    uint32_t rgb = 0xFFFFFFu;
    LedEffect effect = LedEffect::Solid;
    uint8_t params[kLedParamCount] = {0, 0, 0, 0, 0};
    uint32_t effectRgb = 0xFFFFFFu;
    uint16_t count = 0;  // LED count, read-only on the wire
    bool operator==(const LedStripState& other) const;
    bool operator!=(const LedStripState& other) const { return !(*this == other); }
};

struct LedSlot {
    FixedString<kLedIdCapacity> id;
    FixedString<kLedNameCapacity> name;
    bool used = false;
    bool paired = false;
    bool online = false;
    bool stateKnown = false;
    Millis lastSeenMs = 0;
    LedStripState state;
    LedSource lastSource = LedSource::Unknown;
    uint32_t lastSeq = 0;
    uint32_t stateGeneration = 0;  // bumped by every accepted state, so a follower sees each one
    bool operator==(const LedSlot& other) const;
    bool operator!=(const LedSlot& other) const { return !(*this == other); }
};

struct LedRegistryState {
    static constexpr size_t kSlots = 8;
    LedSlot slots[kSlots];
    int8_t selected = -1;
    uint8_t used = 0;
    uint32_t refusedAnnounces = 0;
    bool operator==(const LedRegistryState& other) const;
    bool operator!=(const LedRegistryState& other) const { return !(*this == other); }
};

class LedRegistryModel : public ModelBase<LedRegistryState, 8> {
public:
    static constexpr size_t kSlots = LedRegistryState::kSlots;
    static constexpr Millis kPresenceTimeoutMs = 120000;

    // Record kinds. The field byte is (kind << 3) | slot: one coalescing key per strip and kind.
    enum class Kind : uint8_t {
        Status = 0,          // value = bool online (never-drop)
        Seen = 1,            // no operand: stamps lastSeenMs with the drain time (coalesce)
        Power = 2,           // value = bool (coalesce)
        BrightnessWire = 3,  // value = 0..255 (coalesce)
        Color = 4,           // value = 0xRRGGBB (coalesce)
        Effect = 5,          // value = LedEffect (coalesce)
        ParamsLow = 6,       // value = params 0..3 packed little-endian (coalesce)
        ParamsHigh = 7,      // value = param 4, aux = LED count (coalesce)
        EffectColor = 8,     // value = 0xRRGGBB (coalesce)
        Source = 9,          // value = LedSource, aux = seq (coalesce)
        Paired = 10,         // value = bool (never-drop)
        Select = 11,         // value = slot or -1; the slot bits are unused (never-drop)
        Forget = 12,         // frees the slot (never-drop)
        Count = 13
    };
    static uint8_t fieldId(Kind kind, int slot);
    static Kind kindOf(uint8_t field) { return static_cast<Kind>(field >> 3); }
    static int slotOf(uint8_t field) { return static_cast<int>(field & 0x07u); }
    static RecordClass classOf(Kind kind);
    static ModelUpdate make(Kind kind, int slot, int32_t value, uint32_t atMs, int32_t aux = 0);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    // Display-task setters (the bridge drains its inbound frames on the display task).
    int announce(const char* id, const char* name, Millis now);  // the slot, or -1 when all taken
    int find(const char* id) const;
    bool setOnline(int slot, bool online, Millis now);
    bool noteSeen(int slot, Millis now);
    bool applyState(int slot, const LedStripState& s, LedSource source, uint32_t seq, Millis now);
    bool setPower(int slot, bool power, Millis now);
    bool setBrightnessFromWire(int slot, int wire, Millis now);
    bool setColor(int slot, uint32_t rgb, Millis now);
    bool setEffect(int slot, LedEffect effect, Millis now);
    bool setParams(int slot, const uint8_t params[kLedParamCount], Millis now);
    bool setEffectColor(int slot, uint32_t rgb, Millis now);
    bool setCount(int slot, int count, Millis now);
    bool setSource(int slot, LedSource source, uint32_t seq);
    bool setPaired(int slot, bool paired);
    bool select(int slot);  // -1 clears; an unused slot is refused
    bool selectById(const char* id);
    bool forget(int slot);  // frees the slot; a selection pointing at it goes to none
    void tick(Millis now);  // the presence timeout

    bool validSlot(int index) const {
        return index >= 0 && index < static_cast<int>(kSlots) && state().slots[index].used;
    }
    const LedSlot* slot(int index) const { return validSlot(index) ? &state().slots[index] : nullptr; }
    const LedSlot* selectedSlot() const { return slot(state().selected); }
    int selectedIndex() const { return state().selected; }
    size_t usedCount() const { return state().used; }
    size_t pairedCount() const;
    size_t onlineCount() const;
    // The LIGHT screen may send a command: the strip is known, online and mine.
    bool commandsAllowed(int index) const;
    uint32_t refusedAnnounces() const { return state().refusedAnnounces; }

private:
    bool touch(int index, Millis now);  // the shared "a message arrived" bookkeeping
};

}  // namespace model
