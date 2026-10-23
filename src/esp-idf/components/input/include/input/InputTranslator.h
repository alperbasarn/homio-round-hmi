#pragma once

// INPT-07 / INPT-08 / INPT-11: the pure core of the InputRouter. It turns the latched touch
// flags of one poll and the knob tokens into InputEvents in a fixed ring, applies the D13 back
// gate, the software double-tap counter, the stale-release guard and the wake consumption, and
// keeps the activity clock. No driver, no RTOS, no heap: it compiles and is tested on the host.

#include <cstddef>
#include <cstdint>

#include "input/HitZones.h"
#include "input/InputEvent.h"

namespace input {

// One poll of the touch panel, as the driver latches it (TouchPanel::handleTouchPanel()).
struct TouchSample {
    bool pressed = false;         // finger currently down
    bool newPress = false;        // has_new_press
    bool newRelease = false;      // has_new_release (tap release)
    bool newHoldRelease = false;  // has_new_hold_release
    bool newGesture = false;      // has_new_gesture
    uint8_t gestureCode = 0;      // the driver's touch_gesture_t value (see kGesture* below)
    int16_t x = 0;
    int16_t y = 0;
    uint32_t pressDurationMs = 0; // getLastPressDurationMs() on release polls
};

// The driver's gesture codes, copied so this unit stays driver-free (touch_gesture_t).
constexpr uint8_t kGestureNone = 0x00;
constexpr uint8_t kGestureSwipeUp = 0x01;
constexpr uint8_t kGestureSwipeDown = 0x02;
constexpr uint8_t kGestureSwipeLeft = 0x03;
constexpr uint8_t kGestureSwipeRight = 0x04;
constexpr uint8_t kGestureSingleClick = 0x05;
constexpr uint8_t kGestureDoubleClick = 0x0B;
constexpr uint8_t kGestureLongPress = 0x0C;
constexpr uint8_t kGestureTapRelease = 0x20;
constexpr uint8_t kGestureHoldRelease = 0x21;

class InputTranslator {
public:
    static constexpr size_t kRingCapacity = 8;
    static constexpr uint32_t kBackGateMs = 3000;        // D13
    static constexpr uint32_t kDoubleTapWindowMs = 1000;  // INPT-08
    static constexpr uint32_t kStaleWindowMs = 250;       // INPT-08 / SCRN-25 entry window
    static constexpr int16_t kMaxDeltaPerToken = 10;      // D5 clamp for +N / -N and bursts

    void configure(const HitGeometry& geometry) { geometry_ = geometry; }
    const HitGeometry& geometry() const { return geometry_; }

    // Feed one poll. Emits zero or more events.
    void onTouch(const TouchSample& sample, uint32_t nowMs);
    // Feed one knob token ("+", "-", "+N", "-N", "setpoint=N", anything else = link activity).
    void onKnobToken(const char* token, uint32_t nowMs);
    // Feed an already-parsed detent sum (PCKNOB-11: the link clamps per token, the sum of one poll
    // is passed through, saturated to the record width).
    void onKnobDelta(int delta, uint32_t nowMs);

    // Releases and gestures inside the window are dropped: the gesture that entered a screen must
    // not act on the screen it entered (SCRN-25).
    void beginStaleWindow(uint32_t nowMs);
    // The next press and its release wake the device and are not delivered (INPT-11).
    void armWakeConsumption();
    bool wakeArmed() const { return wakeArmed_ || wakeConsuming_; }

    bool next(InputEvent& out);
    size_t pending() const { return count_; }
    uint32_t overflow() const { return overflow_; }
    size_t highWater() const { return highWater_; }
    uint32_t lastActivityMs() const { return lastActivityMs_; }
    bool touchDown() const { return touchDown_; }

private:
    void push(const InputEvent& event, uint32_t nowMs);
    bool insideStaleWindow(uint32_t nowMs) const;
    static int16_t clampDelta(long value);

    InputEvent ring_[kRingCapacity] = {};
    size_t head_ = 0;
    size_t count_ = 0;
    size_t highWater_ = 0;
    uint32_t overflow_ = 0;
    uint32_t lastActivityMs_ = 0;

    HitGeometry geometry_ = {};
    bool touchDown_ = false;
    bool pressSeen_ = false;      // a press was delivered for the current touch
    bool wakeArmed_ = false;
    bool wakeConsuming_ = false;  // the current touch is the wake tap
    bool staleActive_ = false;
    uint32_t staleUntilMs_ = 0;
    bool lastTapValid_ = false;
    uint32_t lastTapMs_ = 0;
    int16_t lastX_ = 0;
    int16_t lastY_ = 0;
};

}  // namespace input
