#pragma once

// INPT-06: the one event type every input source is translated into. The InputRouter (WP2.1)
// emits it to the active screen controller and to the activity tracker; nothing else polls the
// touch panel or the knob after WP2.3.
//
// The knob has no push button: it only reports rotation (KnobDelta, KnobBurst) and link
// activity. Every press, release and gesture comes from the touch panel.

#include <cstdint>

#include "input/HitZones.h"

namespace input {

enum class InputType : uint8_t {
    None = 0,
    TouchPress,        // finger down; x, y, zone valid
    TouchMove,         // synthesised every poll while pressed; x, y valid
    TouchRelease,      // finger up after a short press; durationMs, zone valid
    TouchHoldRelease,  // finger up after a hold of at least 3000 ms (D13); shorter holds are consumed
    Gesture,           // a swipe or a tap gesture; gesture valid, x, y at the gesture origin
    KnobDelta,         // signed detents since the last event; knobDelta valid (D5)
    KnobBurst,         // a coalesced absolute "setpoint=N" token; knobDelta carries N (D5 converts)
    KnobLink,          // a token that carried no movement (link alive)
    KnobAck,           // "ok:setpoint=N": the knob confirmed a write; knobDelta carries N (PCKNOB-16)
};

enum class Gesture : uint8_t {
    None = 0,
    SwipeUp,
    SwipeDown,
    SwipeLeft,
    SwipeRight,
    SingleTap,
    DoubleTap,
    LongPress,
};

struct InputEvent {
    InputType type = InputType::None;
    Gesture gesture = Gesture::None;
    HitZone zone = HitZone::None;
    int16_t x = 0;
    int16_t y = 0;
    int16_t knobDelta = 0;    // detents, positive clockwise; N for KnobBurst
    uint16_t durationMs = 0;  // press duration on release events, saturating at 65535 (12-byte record)
};

static_assert(sizeof(InputEvent) < 16, "InputEvent must stay a small trivially copyable record");

const char* toString(InputType type);
const char* toString(Gesture gesture);

}  // namespace input
