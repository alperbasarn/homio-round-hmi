#pragma once

#include "LGFX_Config.hpp"
#include <lvgl.h>

class LvglDisplay {
public:
    static bool init(LGFX* graphics);
    static bool isInitialized();
    static void taskHandler();
    static void invalidateScreen();

private:
    static void flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorBuffer);
    static void rounderCallback(lv_disp_drv_t* disp, lv_area_t* area);
    static void tickCallback(void* arg);
};
