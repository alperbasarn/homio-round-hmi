#pragma once

#include "DisplayPanelBackend.h"
#include "TouchPanel.h"
#include <cstdint>
#include <lvgl.h>

class ModeController {
public:
    enum Mode {
        MODE_LIGHT = 0,
        MODE_TEMPERATURE = 1,
        MODE_SOUND = 2,
        MODE_COUNT = 3
    };

private:
    DisplayPanelBackend* display;
    TouchPanel* touchPanel;
    Mode currentMode;
    Mode lastMode;
    bool active;
    bool modeSelected;
    bool pageBackRequested;
    bool ignoreNextRelease;
    int64_t activatedAtMs;
    int tapReleaseCount;
    int64_t firstTapReleaseAtMs;

    lv_obj_t* root;
    lv_obj_t* iconLabel;
    lv_obj_t* titleLabel;
    lv_obj_t* hintLabel;

    static constexpr int64_t STALE_RELEASE_GUARD_MS = 250;
    static constexpr int64_t DOUBLE_TAP_WINDOW_MS = 1000;
    static constexpr int64_t SHORT_HOLD_RELEASE_MS = 3000;

    int64_t millis() const;
    int scalePx(int referencePx) const;
    const char* modeToString(Mode mode) const;
    const char* modeIcon(Mode mode) const;
    void ensureUi();
    void drawModeLogo();

public:
    ModeController(DisplayPanelBackend* displayBackend, TouchPanel* touch);

    void updateScreen();
    void nextMode();
    void previousMode();
    void setActive(bool isActive);
    void setMode(Mode mode);

    Mode getCurrentMode() const { return currentMode; }
    bool isSoundModeActive() const { return currentMode == MODE_SOUND; }
    bool isLightModeActive() const { return currentMode == MODE_LIGHT; }
    bool isTemperatureModeActive() const { return currentMode == MODE_TEMPERATURE; }

    bool isModeSelected() const { return modeSelected; }
    void resetModeSelected() { modeSelected = false; }
    bool isPageBackRequested() const { return pageBackRequested; }
    void resetPageBackRequest() { pageBackRequested = false; }
};
