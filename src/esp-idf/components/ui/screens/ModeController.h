#pragma once

#include "LvglDisplay.h"
#include "TouchPanel.h"
#include <lvgl.h>
#include <cstdint>

class ModeController {
public:
    enum Mode {
        MODE_LIGHT = 0,
        MODE_TEMPERATURE = 1,
        MODE_SOUND = 2,
        MODE_COUNT = 3
    };

private:
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

    // LVGL objects
    lv_obj_t* root;
    lv_obj_t* iconObj;
    lv_obj_t* nameLabel;
    lv_obj_t* hintLabel;
    bool lvglReady;

    static constexpr int64_t STALE_RELEASE_GUARD_MS = 250;
    static constexpr int64_t DOUBLE_TAP_WINDOW_MS   = 1000;
    static constexpr int64_t SHORT_HOLD_RELEASE_MS  = 3000;

    int64_t millis() const;
    const char* modeToString(Mode mode) const;
    void ensureUi();
    void buildUi();
    void refreshUi();

public:
    ModeController(TouchPanel* touch);

    void updateScreen();
    void nextMode();
    void previousMode();
    void setActive(bool isActive);
    void setMode(Mode mode);
    void deactivate();

    Mode getCurrentMode() const { return currentMode; }
    bool isSoundModeActive()       const { return currentMode == MODE_SOUND; }
    bool isLightModeActive()       const { return currentMode == MODE_LIGHT; }
    bool isTemperatureModeActive() const { return currentMode == MODE_TEMPERATURE; }

    bool isModeSelected()       const { return modeSelected; }
    void resetModeSelected()          { modeSelected = false; }
    bool isPageBackRequested()  const { return pageBackRequested; }
    void resetPageBackRequest()       { pageBackRequested = false; }
};
