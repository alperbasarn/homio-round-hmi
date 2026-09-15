#pragma once
// DeviceInfoScreenController (SCRN-11): the DEVICE_INFO page's controller. It reads the
// connectivity and power models every frame instead of polling four callbacks every 2 s (pulling
// the AP now flips the glyph as soon as the poller posts, with no touch), resolves the two bar
// drags with the geometry the view draws, and owns the page's navigation.
//
//   drag on the left bar    display brightness, through the injected accessor
//   drag on the right bar   media volume: a VolumeLocal record plus the active target's setVolume
//   release on the bottom band   back to INFO
//   release on an edge      one step through the matrix column (the router resolves it)
//   swipe                   consumed, as on the page today
//
// `lastActivityTime`, `resetScreen` and the software-update state of the old screen are gone
// (dead register). Brightness has no model yet, so the composition injects a getter and a setter;
// when a row gives the display its own model this pair becomes a record like the others.
#include <cstdint>

#include "DeviceInfoScreenState.h"
#include "IScreenView.h"
#include "ScreenController.h"
#include "input/InputEvent.h"
#include "media_target/MediaTargetRegistry.h"
#include "model/AppModel.h"

namespace controllers {

class DeviceInfoScreenController final : public ScreenController {
public:
    using PercentGetter = int (*)(void* context);
    using PercentSetter = void (*)(int percent, void* context);

    DeviceInfoScreenController(model::AppModel& app, media::MediaTargetRegistry& targets,
                               ui::ScreenView<ui::DeviceInfoScreenState>* view, int panelWidthPx,
                               int panelHeightPx);

    // The display owns the backlight; the page only moves it.
    void setBrightnessAccess(PercentGetter getter, PercentSetter setter, void* context);

    model::AppScreen screen() const override { return model::AppScreen::DeviceInfo; }
    void onEnter(uint32_t nowMs) override;
    void onExit(uint32_t nowMs) override;
    bool onInput(const input::InputEvent& event, uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;

    // The matrix column step an edge release asked for: -1, +1, or 0 when none. Destructive, like
    // the navigation outbox; the router turns it into a page move (DISP-11 retires the matrix).
    int takePageStep();
    bool hasPageStep() const { return pageStep_ != 0; }

private:
    int brightness() const;
    void setBrightness(int percent, uint32_t nowMs);
    void setSound(int percent, uint32_t nowMs);

    model::AppModel& app_;
    media::MediaTargetRegistry& targets_;
    ui::ScreenView<ui::DeviceInfoScreenState>* view_;
    ui::deviceinfo::Layout layout_;
    PercentGetter brightnessGetter_ = nullptr;
    PercentSetter brightnessSetter_ = nullptr;
    void* brightnessContext_ = nullptr;
    int brightnessShadow_ = 100;  // the last value we set, until the accessor reports it back
    int soundShadow_ = -1;        // the last volume this page sent, so a held finger is not work
    int pageStep_ = 0;
};

}  // namespace controllers
