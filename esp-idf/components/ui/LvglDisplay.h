#pragma once

#include "LGFX_Config.hpp"
#include <lvgl.h>
#include <cstdint>

class LvglDisplay {
public:
    // LVGL lifecycle
    static bool init(LGFX* graphics);
    static bool isInitialized();
    static void taskHandler();
    static void invalidateScreen();

    // Display dimensions and brightness — no LGFX type exposed to callers
    static int  getWidth();
    static int  getHeight();
    static void setBrightness(uint8_t brightness);

    // Raw drawing primitives (bypass LVGL — safe only when LVGL is quiescent)
    static void     fillScreen(uint32_t rgb888);
    static void     fillRect(int x, int y, int w, int h, uint32_t rgb888);
    static void     fillCircle(int cx, int cy, int r, uint32_t rgb888);
    static void     drawCircle(int cx, int cy, int r, uint32_t rgb888);
    static void     fillTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb888);
    static void     drawTriangle(int x0, int y0, int x1, int y1, int x2, int y2, uint32_t rgb888);
    static void     drawLine(int x0, int y0, int x1, int y1, uint32_t rgb888);
    static void     fillArc(int cx, int cy, int outerR, int innerR, float startAngle, float endAngle, uint32_t rgb888);
    static void     drawArc(int cx, int cy, int outerR, int innerR, float startAngle, float endAngle, uint32_t rgb888);
    static void     setTextSize(int scale);
    static void     setTextColor(uint32_t rgb888);
    static void     setCursor(int x, int y);
    static void     print(const char* text);
    static int      textWidth(const char* text);
    static int      fontHeight();
    static uint16_t color565(uint8_t r, uint8_t g, uint8_t b);

private:
    static void flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorBuffer);
    static void rounderCallback(lv_disp_drv_t* disp, lv_area_t* area);
    static void tickCallback(void* arg);
};
