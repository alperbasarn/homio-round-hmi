#pragma once

#include "LvglDisplay.h"
#include "input/InputEvent.h"
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
    Mode currentMode;
    Mode lastMode;
    bool active;
    bool modeSelected;
    bool pageBackRequested;

    // LVGL objects
    lv_obj_t* root;
    lv_obj_t* iconObj;
    lv_obj_t* nameLabel;
    lv_obj_t* hintLabel;
    bool lvglReady;


    int64_t millis() const;
    const char* modeToString(Mode mode) const;
    void ensureUi();
    void buildUi();
    void refreshUi();

public:
    ModeController();
    // INPT-17: swipes turn the wheel, the double-tap gesture selects, a 3 s hold backs out;
    // the router already applied the D13 gate, the double-tap window and the entry window.
    void onInput(const input::InputEvent& event);

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
