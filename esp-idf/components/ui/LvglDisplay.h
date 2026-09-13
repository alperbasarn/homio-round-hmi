#pragma once

#include "DisplayFlushBackend.h"
#include "DisplayPanelBackend.h"
#include <cstdint>
#include <lvgl.h>

class LvglDisplay {
public:
    static bool init(DisplayFlushBackend* backend);
    // Ported screens call this lazily; it only reports the state main.cpp established (or
    // re-runs the registered backend), it never picks a backend by itself.
    static bool init();
    static bool isInitialized();
    static bool lock(int timeoutMs);
    static void unlock();
    static void taskHandler();
    static void invalidateScreen();

    // Screen-facing statics ported with the main screens: geometry and brightness go through
    // the registered backends, fillScreen paints the active LVGL screen background.
    static void setPanelBackend(DisplayPanelBackend* panel);
    static int getWidth();
    static int getHeight();
    static void setBrightness(uint8_t brightness);
    static void fillScreen(uint32_t rgb888);

private:
    static void flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorBuffer);
    static void rounderCallback(lv_disp_drv_t* disp, lv_area_t* area);
    static void tickCallback(void* arg);
};
