#include "input/InputTranslator.h"

#include <cstdlib>
#include <cstring>

namespace input {

namespace {

Gesture mapSwipe(uint8_t code) {
    switch (code) {
        case kGestureSwipeUp: return Gesture::SwipeUp;
        case kGestureSwipeDown: return Gesture::SwipeDown;
        case kGestureSwipeLeft: return Gesture::SwipeLeft;
        case kGestureSwipeRight: return Gesture::SwipeRight;
        case kGestureSingleClick: return Gesture::SingleTap;
        case kGestureDoubleClick: return Gesture::DoubleTap;
        case kGestureLongPress: return Gesture::LongPress;
        default: return Gesture::None;
    }
}

}  // namespace

void InputTranslator::push(const InputEvent& event, uint32_t nowMs) {
    lastActivityMs_ = nowMs;
    if (count_ >= kRingCapacity) {
        ++overflow_;  // the newest event is the one dropped; the ring keeps the order it has
        return;
    }
    ring_[(head_ + count_) % kRingCapacity] = event;
    ++count_;
    if (count_ > highWater_) {
        highWater_ = count_;
    }
}

bool InputTranslator::next(InputEvent& out) {
    if (count_ == 0) {
        return false;
    }
    out = ring_[head_];
    head_ = (head_ + 1) % kRingCapacity;
    --count_;
    return true;
}

bool InputTranslator::insideStaleWindow(uint32_t nowMs) const {
    return staleActive_ && static_cast<int32_t>(staleUntilMs_ - nowMs) > 0;
}

void InputTranslator::beginStaleWindow(uint32_t nowMs) {
    staleActive_ = true;
    staleUntilMs_ = nowMs + kStaleWindowMs;
    lastTapValid_ = false;
}

void InputTranslator::armWakeConsumption() {
    wakeArmed_ = true;
}

int16_t InputTranslator::clampDelta(long value) {
    if (value > kMaxDeltaPerToken) return kMaxDeltaPerToken;
    if (value < -kMaxDeltaPerToken) return -kMaxDeltaPerToken;
    return static_cast<int16_t>(value);
}

void InputTranslator::onTouch(const TouchSample& s, uint32_t nowMs) {
    if (staleActive_ && !insideStaleWindow(nowMs)) {
        staleActive_ = false;
    }
    const HitZone zoneNow = classifyHit(s.x, s.y, geometry_);

    if (s.newPress) {
        touchDown_ = true;
        lastX_ = s.x;
        lastY_ = s.y;
        if (wakeArmed_) {
            // INPT-11: this press woke the device; it and its release are consumed.
            wakeArmed_ = false;
            wakeConsuming_ = true;
            pressSeen_ = false;
            lastActivityMs_ = nowMs;
        } else {
            pressSeen_ = true;
            InputEvent e;
            e.type = InputType::TouchPress;
            e.x = s.x;
            e.y = s.y;
            e.zone = zoneNow;
            push(e, nowMs);
        }
    } else if (s.pressed && touchDown_) {
        lastX_ = s.x;
        lastY_ = s.y;
        if (!wakeConsuming_) {
            InputEvent e;
            e.type = InputType::TouchMove;
            e.x = s.x;
            e.y = s.y;
            e.zone = zoneNow;
            push(e, nowMs);
        } else {
            lastActivityMs_ = nowMs;
        }
    }

    const bool releasing = s.newRelease || s.newHoldRelease;
    if (releasing) {
        touchDown_ = false;
        const bool consumed = wakeConsuming_;
        wakeConsuming_ = false;
        const bool stale = insideStaleWindow(nowMs) || !pressSeen_;
        pressSeen_ = false;
        if (!consumed && !stale) {
            if (s.newHoldRelease) {
                if (s.pressDurationMs >= kBackGateMs) {
                    InputEvent e;
                    e.type = InputType::TouchHoldRelease;
                    e.x = lastX_;
                    e.y = lastY_;
                    e.zone = classifyHit(lastX_, lastY_, geometry_);
                    e.durationMs = static_cast<uint16_t>(s.pressDurationMs > 65535u ? 65535u : s.pressDurationMs);
                    push(e, nowMs);
                }
                // D13: shorter hold-releases are consumed and discarded.
                lastTapValid_ = false;
            } else {
                InputEvent e;
                e.type = InputType::TouchRelease;
                e.x = lastX_;
                e.y = lastY_;
                e.zone = classifyHit(lastX_, lastY_, geometry_);
                e.durationMs = static_cast<uint16_t>(s.pressDurationMs > 65535u ? 65535u : s.pressDurationMs);
                push(e, nowMs);
                if (lastTapValid_ && static_cast<uint32_t>(nowMs - lastTapMs_) <= kDoubleTapWindowMs) {
                    InputEvent d;
                    d.type = InputType::Gesture;
                    d.gesture = Gesture::DoubleTap;
                    d.x = lastX_;
                    d.y = lastY_;
                    d.zone = e.zone;
                    push(d, nowMs);
                    lastTapValid_ = false;
                } else {
                    lastTapValid_ = true;
                    lastTapMs_ = nowMs;
                }
            }
        } else {
            lastActivityMs_ = nowMs;
        }
    }

    if (s.newGesture) {
        const Gesture g = mapSwipe(s.gestureCode);
        // Tap and hold releases already arrived through the release flags above.
        if (g != Gesture::None && !wakeConsuming_ && !insideStaleWindow(nowMs)) {
            InputEvent e;
            e.type = InputType::Gesture;
            e.gesture = g;
            e.x = lastX_;
            e.y = lastY_;
            e.zone = classifyHit(lastX_, lastY_, geometry_);
            push(e, nowMs);
            lastTapValid_ = false;
        } else {
            lastActivityMs_ = nowMs;
        }
    }
}

void InputTranslator::onKnobToken(const char* token, uint32_t nowMs) {
    if (token == nullptr) {
        return;
    }
    InputEvent e;
    const size_t len = std::strlen(token);
    if (len == 0) {
        return;
    }
    if (token[0] == '+' || token[0] == '-') {
        long value = 1;
        if (len > 1) {
            char* end = nullptr;
            value = std::strtol(token + 1, &end, 10);
            if (end == token + 1 || *end != 0 || value <= 0) {
                e.type = InputType::KnobLink;  // "+abc" and friends: alive, no movement
                push(e, nowMs);
                return;
            }
        }
        e.type = InputType::KnobDelta;
        e.knobDelta = clampDelta(token[0] == '+' ? value : -value);
        push(e, nowMs);
        return;
    }
    if (std::strncmp(token, "setpoint=", 9) == 0) {
        char* end = nullptr;
        const long value = std::strtol(token + 9, &end, 10);
        if (end != token + 9 && *end == 0 && value >= 0 && value <= 32767) {
            e.type = InputType::KnobBurst;
            e.knobDelta = static_cast<int16_t>(value);
            push(e, nowMs);
            return;
        }
    }
    e.type = InputType::KnobLink;
    push(e, nowMs);
}

}  // namespace input
