#pragma once

#include "LGFX_Config.hpp"
#include "TouchPanel.h"
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
    LGFX* gfx;
    TouchPanel* touchPanel;
    Mode currentMode;
    Mode lastMode;
    bool active;
    bool modeSelected;        // True when user double taps to select mode
    bool pageBackRequested;   // True when user short-holds (3s) and releases
    bool ignoreNextRelease;   // Ignore stale release after entering HOME while finger is down
    int64_t activatedAtMs;    // HOME activation timestamp for stale release guard
    int tapReleaseCount;
    int64_t firstTapReleaseAtMs;

    static constexpr int64_t STALE_RELEASE_GUARD_MS = 250;
    static constexpr int64_t DOUBLE_TAP_WINDOW_MS = 1000;
    static constexpr int64_t SHORT_HOLD_RELEASE_MS = 3000;

    int64_t millis() const;
    const char* modeToString(Mode mode) const;

    void drawThermometer();
    void drawBulb();
    void drawSpeaker();
    void drawModeLogo();

public:
    ModeController(LGFX* graphics, TouchPanel* touch);

    void updateScreen();
    void nextMode();
    void previousMode();
    void setActive(bool isActive);
    void setMode(Mode mode);

    Mode getCurrentMode() const { return currentMode; }
    bool isSoundModeActive() const { return currentMode == MODE_SOUND; }
    bool isLightModeActive() const { return currentMode == MODE_LIGHT; }
    bool isTemperatureModeActive() const { return currentMode == MODE_TEMPERATURE; }

    // Selection handling
    bool isModeSelected() const { return modeSelected; }
    void resetModeSelected() { modeSelected = false; }
    bool isPageBackRequested() const { return pageBackRequested; }
    void resetPageBackRequest() { pageBackRequested = false; }
};
