#include "input/InputSfxListener.h"

namespace input {

bool InputSfxListener::onEvent(const InputEvent& event) {
    if (feedback_ == nullptr) {
        return false;
    }
    switch (event.type) {
        case InputType::TouchPress:
            feedback_->onTouchPress();
            return true;
        case InputType::TouchRelease:
        case InputType::TouchHoldRelease:
            feedback_->onTouchRelease();
            return true;
        case InputType::Gesture:
            switch (event.gesture) {
                case Gesture::SwipeUp:
                case Gesture::SwipeDown:
                case Gesture::SwipeLeft:
                case Gesture::SwipeRight:
                    feedback_->onSwipe(event.gesture);
                    return true;
                default:
                    return false;
            }
        default:
            return false;
    }
}

}  // namespace input
