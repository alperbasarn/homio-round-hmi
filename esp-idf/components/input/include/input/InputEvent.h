#pragma once

// INPT-06: the one event type every input source is translated into. The InputRouter (WP2.1)
// emits it to the active screen controller and to the activity tracker; nothing else polls the
// touch panel or the knob after WP2.3.
//
// The knob has no push button: it only reports rotation (KnobDelta) and link activity. Every
// press, release and gesture comes from the touch panel.

#include <cstdint>

namespace input {

enum class InputType : uint8_t {
    None = 0,
    TouchPress,        // finger down; x, y valid
    TouchMove,         // synthesised every poll while pressed; x, y valid
    TouchRelease,      // finger up after a short press; durationMs valid
    TouchHoldRelease,  // finger up after a hold; durationMs valid (D13 gates the back gesture on it)
    Gesture,           // one of the swipes or taps; gesture valid, x, y at the gesture origin
    KnobDelta,         // signed detents since the last event; knobDelta valid (D5)
    KnobLink,          // a token that carried no delta (link alive, no movement)
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
    int16_t x = 0;
    int16_t y = 0;
    int16_t knobDelta = 0;    // detents, positive clockwise
    uint32_t durationMs = 0;  // press duration on release events, else 0 (last: keeps sizeof at 12)
};

static_assert(sizeof(InputEvent) < 16, "InputEvent must stay a small trivially copyable record");

const char* toString(InputType type);
const char* toString(Gesture gesture);

}  // namespace input
