#pragma once

#include "LGFX_Config.hpp"
#include "LvglDisplay.h"
#include <lvgl.h>
#include <cstdint>
#include <algorithm>

class InitializationScreen {
private:
    LGFX* gfx;
    bool lvglReady;
    bool screenInitialized;

    // LVGL widgets
    lv_obj_t* root;
    lv_obj_t* progressArc;
    lv_obj_t* qLabel;
    lv_obj_t* nobLabel;

    // Progress animation state
    int currentArcValue;  // 0-100
    int targetArcValue;   // 0-100

    static constexpr float ARC_START_ANGLE  = 120.0f;
    static constexpr float ARC_SPAN         = 300.0f;
    static constexpr int   ANIMATION_DURATION = 500;  // ms

    static int16_t normalizeAngle(float degrees);
    void ensureUi();
    void buildUi();
    void applyArcValue(int value);
    static void arcAnimExec(void* var, int32_t value);
    int scalePx(int referencePx) const;

public:
    explicit InitializationScreen(LGFX* graphics);

    void updateScreen();
    void setProgress(int progress);
    void setWiFiStatus(bool connected, int strength);
    void setMQTTStatus(bool isConnected);
    void reset();
    void hideScreen();
};
