#pragma once

// UIX-06 / D12: the one hit-test helper. Coordinates are screen pixels; the radii are the design
// numbers for the 466 px round panel (disc r < 132, or 145 on the climate screen; rim annulus
// 140 <= r <= 190; everything else dead) and scale linearly with the shorter screen side.
// LV_OBJ_FLAG_CLICKABLE is inert in this tree: no LVGL input device exists, touch is polled.

#include <cstdint>

namespace input {

enum class HitZone : uint8_t {
    None = 0,  // no coordinates (knob events)
    Disc,      // the centre disc
    Rim,       // the ring around the disc
    Dead,      // the gap between disc and rim, or outside the rim
};

struct HitGeometry {
    int32_t width = 466;
    int32_t height = 466;
    bool climate = false;  // the climate screen grows the disc to r < 145
};

inline HitZone classifyHit(int32_t x, int32_t y, const HitGeometry& g) {
    const int32_t side = g.width < g.height ? g.width : g.height;
    if (side <= 0) {
        return HitZone::Dead;
    }
    // Work in the 466 px design space so the thresholds stay the documented numbers.
    const int32_t dx = (x - g.width / 2) * 466 / side;
    const int32_t dy = (y - g.height / 2) * 466 / side;
    const int32_t r2 = dx * dx + dy * dy;
    const int32_t discR = g.climate ? 145 : 132;
    if (r2 < discR * discR) {
        return HitZone::Disc;
    }
    if (r2 >= 140 * 140 && r2 <= 190 * 190) {
        return HitZone::Rim;
    }
    return HitZone::Dead;
}

const char* toString(HitZone zone);

}  // namespace input
