#include "DeviceInfoView.h"

#include <algorithm>
#include <cmath>

#include "LvglDisplay.h"
#include "sdkconfig.h"

namespace ui {

namespace {
constexpr uint32_t kBlack = 0x000000u;
constexpr uint32_t kWhite = 0xFFFFFFu;
constexpr uint32_t kDarkGrey = 0x242A31u;
constexpr uint32_t kGrey = 0x7B8794u;
constexpr uint32_t kGreen = 0x74E365u;
constexpr uint32_t kRed = 0xFF4C4Cu;
constexpr uint32_t kBluetooth = 0x2F80FFu;

int clampPercent(int value) {
    return std::max(0, std::min(100, value));
}
}  // namespace

void DeviceInfoView::doBuild(lv_obj_t* parent) {
    layout_ = deviceinfo::layoutFor(panelWidth(), panelHeight());
    const deviceinfo::Layout& l = layout_;

    root_ = lv_obj_create(parent != nullptr ? parent : lv_scr_act());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, l.panelW, l.panelH);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(kBlack), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* bluetoothContainer = lv_obj_create(root_);
    lv_obj_remove_style_all(bluetoothContainer);
    lv_obj_set_size(bluetoothContainer, l.iconW, l.iconH);
    lv_obj_set_pos(bluetoothContainer, l.iconX, l.bluetoothY);
    lv_obj_clear_flag(bluetoothContainer, LV_OBJ_FLAG_SCROLLABLE);

    bluetoothIcon_ = lv_label_create(bluetoothContainer);
    lv_obj_set_style_text_align(bluetoothIcon_, LV_TEXT_ALIGN_CENTER, 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_32)
    lv_obj_set_style_text_font(bluetoothIcon_, &lv_font_montserrat_32, 0);
#endif
    lv_label_set_text(bluetoothIcon_, LV_SYMBOL_BLUETOOTH);
    lv_obj_set_style_text_color(bluetoothIcon_, lv_color_hex(kGrey), 0);
    lv_obj_center(bluetoothIcon_);

    wifiContainer_ = lv_obj_create(root_);
    lv_obj_remove_style_all(wifiContainer_);
    lv_obj_set_size(wifiContainer_, l.iconW, l.iconH);
    lv_obj_set_pos(wifiContainer_, l.iconX, l.wifiY);
    lv_obj_clear_flag(wifiContainer_, LV_OBJ_FLAG_SCROLLABLE);

    const auto createWifiArc = [&](int size, int yOffset) -> lv_obj_t* {
        lv_obj_t* arc = lv_arc_create(wifiContainer_);
        lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
        lv_obj_set_size(arc, size, size);
        lv_obj_align(arc, LV_ALIGN_BOTTOM_MID, 0, yOffset);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, std::max(3, scalePx(4)), LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
        lv_arc_set_range(arc, 0, 100);
        lv_arc_set_value(arc, 100);
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_angles(arc, 215, 325);
        return arc;
    };
    wifiArcOuter_ = createWifiArc(std::min(l.iconW, l.iconH + scalePx(14)), scalePx(9));
    wifiArcMiddle_ = createWifiArc(std::min(l.iconW - scalePx(11), l.iconH + scalePx(3)), scalePx(6));
    wifiArcInner_ = createWifiArc(std::min(l.iconW - scalePx(22), l.iconH - scalePx(7)), scalePx(3));

    wifiDot_ = lv_obj_create(wifiContainer_);
    lv_obj_remove_style_all(wifiDot_);
    lv_obj_set_size(wifiDot_, scalePx(5), scalePx(5));
    lv_obj_set_style_radius(wifiDot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(wifiDot_, LV_OPA_COVER, 0);
    lv_obj_align(wifiDot_, LV_ALIGN_BOTTOM_MID, 0, 0);

    lv_obj_t* batteryContainer = lv_obj_create(root_);
    lv_obj_remove_style_all(batteryContainer);
    lv_obj_set_size(batteryContainer, l.iconW, l.iconH);
    lv_obj_set_pos(batteryContainer, l.iconX, l.batteryY);
    lv_obj_clear_flag(batteryContainer, LV_OBJ_FLAG_SCROLLABLE);

    const int batteryW = std::max(scalePx(23), l.iconW - scalePx(10));
    const int batteryH = std::max(scalePx(11), l.iconH / 2);
    batteryOutline_ = lv_obj_create(batteryContainer);
    lv_obj_remove_style_all(batteryOutline_);
    lv_obj_set_size(batteryOutline_, batteryW, batteryH);
    lv_obj_align(batteryOutline_, LV_ALIGN_TOP_MID, -scalePx(4), scalePx(7));
    lv_obj_set_style_radius(batteryOutline_, scalePx(4), 0);
    lv_obj_set_style_border_width(batteryOutline_, std::max(2, scalePx(2)), 0);
    lv_obj_set_style_border_color(batteryOutline_, lv_color_hex(kGrey), 0);
    lv_obj_set_style_bg_opa(batteryOutline_, LV_OPA_TRANSP, 0);

    batteryFill_ = lv_obj_create(batteryContainer);
    lv_obj_remove_style_all(batteryFill_);
    lv_obj_set_style_radius(batteryFill_, scalePx(3), 0);

    batteryFillX_ = (l.iconW - batteryW) / 2 - scalePx(4);   // TOP_MID with the page's own offsets
    batteryFillY_ = scalePx(7);
    const int innerPad = std::max(3, scalePx(3));
    batteryFillX_ += innerPad;
    batteryFillY_ += innerPad;
    batteryFillMaxW_ = std::max(1, batteryW - 2 * innerPad);
    batteryFillH_ = std::max(1, batteryH - 2 * innerPad);

    batteryCap_ = lv_obj_create(batteryContainer);
    lv_obj_remove_style_all(batteryCap_);
    lv_obj_set_size(batteryCap_, scalePx(3), std::max(scalePx(6), batteryH / 2));
    lv_obj_align_to(batteryCap_, batteryOutline_, LV_ALIGN_OUT_RIGHT_MID, scalePx(2), 0);
    lv_obj_set_style_radius(batteryCap_, scalePx(2), 0);
    lv_obj_set_style_bg_color(batteryCap_, lv_color_hex(kGrey), 0);
    lv_obj_set_style_bg_opa(batteryCap_, LV_OPA_COVER, 0);

    backChevron_ = lv_label_create(root_);
    lv_obj_set_style_text_color(backChevron_, lv_color_hex(kGrey), 0);
    lv_label_set_text(backChevron_, "v");
    lv_obj_align(backChevron_, LV_ALIGN_BOTTOM_MID, 0, -(l.bottomH / 3));

    const auto createBar = [&](const deviceinfo::Rect& r) {
        lv_obj_t* bar = lv_obj_create(root_);
        lv_obj_remove_style_all(bar);
        lv_obj_set_size(bar, r.w, r.h);
        lv_obj_set_pos(bar, r.x, r.y);
        lv_obj_set_style_radius(bar, scalePx(12), 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(kDarkGrey), 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    };
    createBar(l.brightnessBar);
    createBar(l.soundBar);

    const auto createFill = [&]() {
        lv_obj_t* fill = lv_obj_create(root_);
        lv_obj_remove_style_all(fill);
        lv_obj_set_style_radius(fill, scalePx(12), 0);
        lv_obj_set_style_bg_color(fill, lv_color_hex(kWhite), 0);
        lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
        return fill;
    };
    brightnessFill_ = createFill();
    soundFill_ = createFill();

    last_ = Rendered{};
    LvglDisplay::invalidateScreen();
}

void DeviceInfoView::doDestroy() {
    if (root_ != nullptr) {
        lv_obj_del(root_);
    }
    root_ = bluetoothIcon_ = wifiContainer_ = wifiArcOuter_ = wifiArcMiddle_ = wifiArcInner_ = nullptr;
    wifiDot_ = batteryOutline_ = batteryFill_ = batteryCap_ = nullptr;
    brightnessFill_ = soundFill_ = backChevron_ = nullptr;
    last_ = Rendered{};
}

void DeviceInfoView::doSetVisible(bool visible) {
    if (root_ == nullptr) return;
    if (visible) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void DeviceInfoView::setArc(lv_obj_t* arc, bool lit, uint32_t colour) {
    if (arc == nullptr) return;
    lv_obj_set_style_arc_color(arc, lv_color_hex(lit ? colour : kDarkGrey), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, lit ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_INDICATOR);
}

// MDL-22 / SCRN-10: the model reports 0..4 bars and the glyph has four levels, the dot plus the
// three arcs. The old page clamped to three and forced three arcs on a lost link, so the top bar
// was unreachable and a connected-but-weak AP looked like a disconnected one.
void DeviceInfoView::renderSignal(int bars, uint32_t colour) {
    setArc(wifiArcInner_, bars >= 2, colour);
    setArc(wifiArcMiddle_, bars >= 3, colour);
    setArc(wifiArcOuter_, bars >= 4, colour);
    // The dot is the "link present" mark the old page always painted, so a connected link at zero
    // bars still shows one; dimming it instead of unlighting it keeps zero and one bar apart.
    lv_obj_set_style_bg_color(wifiDot_, lv_color_hex(colour), 0);
    lv_obj_set_style_bg_opa(wifiDot_, bars >= 1 ? LV_OPA_COVER : LV_OPA_50, 0);
}

// SCRN-10: band to colour only. Five looks, one per classification (the sixth enumerator, the
// PMU's "presence unknown", shares the grey outline of "no reading").
void DeviceInfoView::renderBattery(deviceinfo::BatteryVisual visual, int percent) {
    uint32_t outline = kGrey;
    uint32_t fill = kGrey;
    bool showFill = false;
    switch (visual) {
        case deviceinfo::BatteryVisual::Unknown: break;
        case deviceinfo::BatteryVisual::Absent: outline = kDarkGrey; fill = kDarkGrey; break;
        case deviceinfo::BatteryVisual::NoReading: break;
        case deviceinfo::BatteryVisual::Critical: outline = fill = kRed; showFill = true; break;
        case deviceinfo::BatteryVisual::Ok: outline = fill = kGreen; showFill = true; break;
        case deviceinfo::BatteryVisual::Charging: outline = kWhite; fill = kGreen; showFill = true; break;
        default: break;
    }
    lv_obj_set_style_border_color(batteryOutline_, lv_color_hex(outline), 0);
    lv_obj_set_style_bg_color(batteryCap_, lv_color_hex(outline), 0);

    const int fillW = showFill ? std::max(1, (batteryFillMaxW_ * clampPercent(percent)) / 100) : 1;
    lv_obj_set_pos(batteryFill_, batteryFillX_, batteryFillY_);
    lv_obj_set_size(batteryFill_, fillW, batteryFillH_);
    lv_obj_set_style_bg_color(batteryFill_, lv_color_hex(fill), 0);
    lv_obj_set_style_bg_opa(batteryFill_, showFill ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
}

void DeviceInfoView::setBarFill(lv_obj_t* fill, const deviceinfo::Rect& bar, int percent) {
    if (fill == nullptr) return;
    const int clamped = clampPercent(percent);
    const int fillH = std::max(1, (bar.h * clamped) / 100);
    lv_obj_set_pos(fill, bar.x, bar.y + bar.h - fillH);
    lv_obj_set_size(fill, bar.w, fillH);
}

void DeviceInfoView::renderBars(int brightnessPercent, int soundPercent) {
    setBarFill(brightnessFill_, layout_.brightnessBar, brightnessPercent);
    setBarFill(soundFill_, layout_.soundBar, soundPercent);
}

void DeviceInfoView::doRender(const DeviceInfoScreenState& state) {
    if (state.connectivity == nullptr || state.power == nullptr || root_ == nullptr) {
        return;
    }
    const model::ConnectivityState& c = *state.connectivity;
    const model::PowerState& p = *state.power;

    Rendered now;
    now.signalLevels = deviceinfo::signalLevels(c);
    now.signalColour = !c.wifiConnected ? kRed : (c.internetAvailable ? kGreen : kGrey);
    now.battery = deviceinfo::batteryVisualFor(p);
    now.batteryPercent = p.percentValid ? clampPercent(static_cast<int>(std::lround(p.percent))) : -1;
    now.brightness = clampPercent(state.brightnessPercent);
    now.sound = clampPercent(state.soundPercent);
    now.bluetooth = state.bluetoothConnected;
    now.valid = true;

    if (!last_.valid || last_.signalLevels != now.signalLevels || last_.signalColour != now.signalColour) {
        renderSignal(now.signalLevels, now.signalColour);
    }
    if (!last_.valid || last_.battery != now.battery || last_.batteryPercent != now.batteryPercent) {
        renderBattery(now.battery, now.batteryPercent < 0 ? 0 : now.batteryPercent);
    }
    if (!last_.valid || last_.brightness != now.brightness || last_.sound != now.sound) {
        renderBars(now.brightness, now.sound);
    }
    if (!last_.valid || last_.bluetooth != now.bluetooth) {
        lv_obj_set_style_text_color(bluetoothIcon_, lv_color_hex(now.bluetooth ? kBluetooth : kGrey), 0);
    }
    last_ = now;
}

}  // namespace ui
