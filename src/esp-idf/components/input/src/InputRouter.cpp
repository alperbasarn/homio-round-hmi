#include "input/InputRouter.h"

#include "KnobController.h"
#include "TouchPanel.h"

namespace input {

InputRouter::InputRouter(TouchPanel* touch, KnobController* knob) : touch_(touch), knob_(knob) {}

void InputRouter::poll(uint32_t nowMs) {
    knobTokenThisPoll_ = false;
    if (touch_ != nullptr) {
        touch_->handleTouchPanel();
        TouchSample s;
        s.pressed = touch_->isPressed();
        s.newPress = touch_->getHasNewPress();
        s.newRelease = touch_->getHasNewRelease();
        s.newHoldRelease = touch_->getHasNewHoldRelease();
        s.newGesture = touch_->getHasNewGesture();
        s.gestureCode = static_cast<uint8_t>(touch_->getLastGesture());
        s.x = static_cast<int16_t>(touch_->getTouchX());
        s.y = static_cast<int16_t>(touch_->getTouchY());
        const int64_t duration = touch_->getLastPressDurationMs();
        s.pressDurationMs = duration > 0 ? static_cast<uint32_t>(duration) : 0;
        translator_.onTouch(s, nowMs);
    }
    if (knob_ != nullptr) {
        knob_->update();
        // PCKNOB-11: detents are summed by the link and cleared here; every other line is a message.
        const int delta = knob_->consumeDelta();
        if (delta != 0) {
            knobTokenThisPoll_ = true;
            translator_.onKnobDelta(delta, nowMs);
        }
        if (knob_->getHasNewMessage()) {
            knobTokenThisPoll_ = true;
            const std::string token = knob_->getReceivedCommand();
            translator_.onKnobToken(token.c_str(), nowMs);
        }
    }
}

}  // namespace input
