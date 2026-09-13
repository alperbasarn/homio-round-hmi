#include "input/InputEvent.h"

namespace input {

const char* toString(InputType type) {
    switch (type) {
        case InputType::None: return "none";
        case InputType::TouchPress: return "press";
        case InputType::TouchMove: return "move";
        case InputType::TouchRelease: return "release";
        case InputType::TouchHoldRelease: return "hold-release";
        case InputType::Gesture: return "gesture";
        case InputType::KnobDelta: return "knob-delta";
        case InputType::KnobLink: return "knob-link";
    }
    return "?";
}

const char* toString(Gesture gesture) {
    switch (gesture) {
        case Gesture::None: return "none";
        case Gesture::SwipeUp: return "swipe-up";
        case Gesture::SwipeDown: return "swipe-down";
        case Gesture::SwipeLeft: return "swipe-left";
        case Gesture::SwipeRight: return "swipe-right";
        case Gesture::SingleTap: return "tap";
        case Gesture::DoubleTap: return "double-tap";
        case Gesture::LongPress: return "long-press";
    }
    return "?";
}

}  // namespace input
