#pragma once
// DeviceInfoView (SCRN-10): the DEVICE_INFO page as a view. The ported layout unchanged (the
// stacked icon column on the right, the brightness and sound bars in the middle, the back
// chevron), with the two defects the row names fixed: the signal glyph now maps the model's
// 0..4 bars onto the dot plus three arcs, so the top bar is reachable, and the battery icon has
// one look per classification instead of one grey look for every unknown. The view owns LVGL
// handles only: it never drives the panel brightness (SCRN-23 leaves that to the router), never
// pumps the LVGL timer (DISP-13), reads no display singleton, and allocates nothing per frame.
#include "DeviceInfoScreenState.h"
#include "IScreenView.h"

namespace ui {

class DeviceInfoView final : public ScreenView<DeviceInfoScreenState> {
public:
    lv_obj_t* root() const override { return root_; }

protected:
    void doBuild(lv_obj_t* parent) override;
    void doDestroy() override;
    void doSetVisible(bool visible) override;
    void doRender(const DeviceInfoScreenState& state) override;

private:
    // Only what the pixels depend on: the signal level and its colour, the battery look and its
    // rounded percent, the two bar percents and the Bluetooth flag. Comparing the whole model
    // state would repaint on every filtered-voltage sample (the old page carried 0.5 % and 20 mV
    // deadbands for that), and a style write invalidates its object even when the value is equal.
    struct Rendered {
        int signalLevels = -1;
        uint32_t signalColour = 0;
        deviceinfo::BatteryVisual battery = deviceinfo::BatteryVisual::Unknown;
        int batteryPercent = -1;
        int brightness = -1;
        int sound = -1;
        bool bluetooth = false;
        bool valid = false;
    };
    void renderSignal(int levels, uint32_t colour);
    void renderBattery(deviceinfo::BatteryVisual visual, int percent);
    void renderBars(int brightnessPercent, int soundPercent);
    void setArc(lv_obj_t* arc, bool lit, uint32_t colour);
    void setBarFill(lv_obj_t* fill, const deviceinfo::Rect& bar, int percent);

    deviceinfo::Layout layout_;
    int batteryFillX_ = 0;
    int batteryFillY_ = 0;
    int batteryFillMaxW_ = 1;
    int batteryFillH_ = 1;
    lv_obj_t* root_ = nullptr;
    lv_obj_t* bluetoothIcon_ = nullptr;
    lv_obj_t* wifiContainer_ = nullptr;
    lv_obj_t* wifiArcOuter_ = nullptr;
    lv_obj_t* wifiArcMiddle_ = nullptr;
    lv_obj_t* wifiArcInner_ = nullptr;
    lv_obj_t* wifiDot_ = nullptr;
    lv_obj_t* batteryOutline_ = nullptr;
    lv_obj_t* batteryFill_ = nullptr;
    lv_obj_t* batteryCap_ = nullptr;
    lv_obj_t* brightnessFill_ = nullptr;
    lv_obj_t* soundFill_ = nullptr;
    lv_obj_t* backChevron_ = nullptr;
    Rendered last_;
};

}  // namespace ui
