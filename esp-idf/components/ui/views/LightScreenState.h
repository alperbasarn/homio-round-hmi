#pragma once
// LightScreenState (LED-05): what the LIGHT view renders, and the rim geometry the view and its
// controller share. Pure: the controller resolves rim taps and ring drags with the same numbers
// the view draws, so a tap lands on the segment the user sees.
#include <cmath>
#include <cstdint>

#include "model/LedRegistryModel.h"
#include "model/LightModel.h"

namespace ui {

struct LightScreenState {
    const model::LightState* light = nullptr;
    const model::LedRegistryState* registry = nullptr;
    int dial = 0;  // the blink count being dialled while a pairing is requested
};

namespace rim {

// Angle of a point around the panel centre: degrees 0..360, clockwise from 12 o'clock.
inline float angleDeg(int x, int y, int panelW, int panelH) {
    const float dx = static_cast<float>(x) - static_cast<float>(panelW) / 2.0f;
    const float dy = static_cast<float>(y) - static_cast<float>(panelH) / 2.0f;
    float deg = std::atan2(dx, -dy) * 180.0f / 3.14159265f;
    if (deg < 0.0f) deg += 360.0f;
    if (deg >= 360.0f) deg -= 360.0f;
    return deg;
}

// Which of `count` equal segments (the first starts at 12 o'clock, clockwise) an angle falls in.
inline int segmentAt(float angle, int count) {
    if (count <= 0) return -1;
    int i = static_cast<int>(angle / (360.0f / static_cast<float>(count)));
    if (i < 0) i = 0;
    if (i >= count) i = count - 1;
    return i;
}

// The chooser order is the slot order of the strips the registry knows.
inline int usedSlotByOrdinal(const model::LedRegistryState& s, int ordinal) {
    int seen = 0;
    for (size_t i = 0; i < model::LedRegistryState::kSlots; ++i) {
        if (!s.slots[i].used) continue;
        if (seen == ordinal) return static_cast<int>(i);
        ++seen;
    }
    return -1;
}

inline int ordinalOfSlot(const model::LedRegistryState& s, int slot) {
    int seen = 0;
    for (size_t i = 0; i < model::LedRegistryState::kSlots; ++i) {
        if (!s.slots[i].used) continue;
        if (static_cast<int>(i) == slot) return seen;
        ++seen;
    }
    return -1;
}

// The chooser shows when there is a choice, or when the only strip still needs pairing.
inline bool chooserVisible(const model::LedRegistryState& s) {
    if (s.used >= 2) return true;
    for (size_t i = 0; i < model::LedRegistryState::kSlots; ++i) {
        if (s.slots[i].used && !s.slots[i].paired) return true;
    }
    return false;
}

inline float hueFromAngle(float angle) {
    return angle / 360.0f;
}

}  // namespace rim
}  // namespace ui
