#pragma once

#include "DisplayFlushBackend.h"
#include <lvgl.h>

class LvglDisplay {
public:
    static bool init(DisplayFlushBackend* backend);
    static bool isInitialized();
    static bool lock(int timeoutMs);
    static void unlock();
    static void taskHandler();
    static void invalidateScreen();

private:
    static void flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorBuffer);
    static void rounderCallback(lv_disp_drv_t* disp, lv_area_t* area);
    static void tickCallback(void* arg);
};
