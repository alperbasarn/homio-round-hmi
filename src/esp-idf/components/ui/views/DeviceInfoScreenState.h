#pragma once
// DeviceInfoScreenState (SCRN-10 / SCRN-11): what the DEVICE_INFO view renders, and the pure
// geometry the view and its controller share, so a drag lands on the bar the user sees and the
// bottom band is the same rectangle in both. Every number here is the ported layout's own
// (DeviceInfoScreen::buildUi and its onInput hit tests), moved out of the screen class and
// expressed from the injected panel size instead of the display singleton (the old `scalePx`
// dereferenced it before `buildUi` had run).
#include <algorithm>
#include <cstdint>

#include "Theme.h"
#include "model/ConnectivityModel.h"
#include "model/PowerModel.h"

namespace ui {

struct DeviceInfoScreenState {
    const model::ConnectivityState* connectivity = nullptr;
    const model::PowerState* power = nullptr;
    int brightnessPercent = 100;
    int soundPercent = 50;
    bool bluetoothConnected = false;  // no BLE in this tree; the icon stays grey
};

namespace deviceinfo {

struct Rect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

inline bool contains(const Rect& r, int x, int y) {
    return x >= r.x && x < (r.x + r.w) && y >= r.y && y < (r.y + r.h);
}

// The five battery looks the row can show. `Unknown` and `NoReading` are both "no information"
// but from different causes: the PMU has not reported presence yet, or it has no percent.
enum class BatteryVisual : uint8_t { Unknown = 0, Absent, NoReading, Critical, Ok, Charging };

inline BatteryVisual batteryVisualFor(const model::PowerState& p) {
    // The reading comes first, exactly as the old page gated it (connected and a valid percent):
    // an ADC board estimates the percent from the voltage with presenceKnown false, and PERI-05
    // left presenceKnown false on every board, so testing presence first would grey out a real
    // reading. Only with no reading do the three unknowns matter.
    if (p.connected && p.percentValid) {
        if (p.charging) return BatteryVisual::Charging;
        return p.band == model::BatteryBand::Critical ? BatteryVisual::Critical : BatteryVisual::Ok;
    }
    if (!p.presenceKnown) return BatteryVisual::Unknown;
    if (!p.connected) return BatteryVisual::Absent;
    return BatteryVisual::NoReading;
}

// How many of the glyph's four levels light up: the dot is level 1 and the three arcs are 2, 3
// and 4, so the model's 0..4 bars all show (the old page clamped to three arcs and the top bar
// was unreachable). A lost link lights every level, which the view then paints red.
inline int signalLevels(const model::ConnectivityState& c) {
    if (!c.wifiConnected) return 4;
    const int bars = static_cast<int>(c.bars);
    return bars < 0 ? 0 : (bars > model::kSignalBarsMax ? model::kSignalBarsMax : bars);
}

struct Layout {
    int panelW = 0;
    int panelH = 0;
    int edgeW = 0;
    int topH = 0;
    int bottomH = 0;
    Rect brightnessBar;
    Rect soundBar;
    Rect brightnessTouch;  // the bar widened by 8 reference px on each side, as the page had it
    Rect soundTouch;
    Rect bottomBand;
    Rect leftEdge;
    Rect rightEdge;
    int iconW = 0;
    int iconH = 0;
    int iconX = 0;
    int bluetoothY = 0;
    int wifiY = 0;
    int batteryY = 0;
};

inline Layout layoutFor(int panelW, int panelH) {
    Layout l;
    l.panelW = panelW;
    l.panelH = panelH;
    const auto sp = [panelW](int referencePx) { return theme::scalePx(referencePx, panelW); };
    l.edgeW = std::max(sp(24), panelW / 10);
    l.topH = std::max(sp(64), panelH / 5);
    l.bottomH = std::max(sp(32), panelH / 10);

    const int barW = std::max(sp(55), (panelW * 6) / 50);
    const int barH = std::min(std::max(sp(134), panelH / 2),
                              panelH - l.topH - l.bottomH - sp(22));
    const int barY = (panelH - barH) / 2;
    const int barGap = sp(10);
    l.soundBar = {(panelW / 2) - (barW / 2), barY, barW, barH};
    l.brightnessBar = {l.soundBar.x - barW - barGap, barY, barW, barH};
    l.brightnessTouch = {l.brightnessBar.x - sp(8), barY, barW + sp(16), barH};
    l.soundTouch = {l.soundBar.x - sp(8), barY, barW + sp(16), barH};

    l.bottomBand = {0, panelH - l.bottomH, panelW, l.bottomH};
    l.leftEdge = {0, 0, l.edgeW, panelH};
    l.rightEdge = {panelW - l.edgeW, 0, l.edgeW, panelH};

    l.iconW = std::max(sp(36), panelW / 8);
    l.iconH = std::max(sp(29), l.topH / 2);
    l.iconX = std::min(panelW - l.edgeW - l.iconW, l.soundBar.x + barW + sp(12));
    const int iconGap = sp(8);
    l.batteryY = std::min(panelH - l.bottomH - l.iconH, l.soundBar.y + barH - l.iconH);
    l.wifiY = std::max(sp(10), l.batteryY - l.iconH - iconGap);
    l.bluetoothY = std::max(sp(4), l.wifiY - l.iconH - iconGap);
    return l;
}

// A y inside a vertical bar as a percent, 0 at the bottom. Clamped to the bar, then to 0..100.
inline int percentFromBarY(int y, int barY, int barH) {
    const int clampedY = std::max(barY, std::min(barY + barH, y));
    const int value = ((barY + barH - clampedY) * 100) / std::max(1, barH);
    return std::max(0, std::min(100, value));
}

}  // namespace deviceinfo
}  // namespace ui
