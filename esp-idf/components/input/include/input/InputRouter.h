#pragma once

// INPT-07: the sole owner of the touch panel and the knob link on the display task. It polls both
// drivers once per frame, feeds the pure InputTranslator, and hands the events out in order. The
// driver headers stay out of this public header (BLD-11: the public surface requires only model).

#include <cstdint>

#include "input/InputEvent.h"
#include "input/InputTranslator.h"

class TouchPanel;
class KnobController;

namespace input {

class InputRouter {
public:
    InputRouter(TouchPanel* touch, KnobController* knob);

    void configure(const HitGeometry& geometry) { translator_.configure(geometry); }

    // Poll both drivers once. Call before anything else reads them in the frame.
    void poll(uint32_t nowMs);

    bool next(InputEvent& out) { return translator_.next(out); }
    void beginStaleWindow(uint32_t nowMs) { translator_.beginStaleWindow(nowMs); }
    void armWakeConsumption() { translator_.armWakeConsumption(); }

    uint32_t lastActivityMs() const { return translator_.lastActivityMs(); }
    uint32_t overflow() const { return translator_.overflow(); }
    size_t highWater() const { return translator_.highWater(); }
    bool touchDown() const { return translator_.touchDown(); }
    // True when the last poll saw a knob token; the legacy consumers read the token themselves
    // until INPT-21 retires them.
    bool knobTokenThisPoll() const { return knobTokenThisPoll_; }

private:
    TouchPanel* touch_;
    KnobController* knob_;
    InputTranslator translator_;
    bool knobTokenThisPoll_ = false;
};

}  // namespace input
