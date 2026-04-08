#include "LvglDisplay.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"

namespace {

constexpr const char* TAG = "LvglDisplay";
constexpr uint32_t LVGL_TICK_PERIOD_MS = 5;
constexpr uint32_t DRAW_BUFFER_LINES_DEFAULT = 50;
constexpr uint32_t DRAW_BUFFER_LINES_MIN = 5;

LGFX* s_gfx = nullptr;
bool s_initialized = false;
lv_disp_draw_buf_t s_drawBuffer;
lv_disp_drv_t s_displayDriver;
lv_disp_t* s_display = nullptr;
lv_color_t* s_bufferA = nullptr;
lv_color_t* s_bufferB = nullptr;
#if LV_COLOR_DEPTH != 16
uint16_t* s_colorConvertBuffer = nullptr;
size_t s_colorConvertBufferPixels = 0;
#endif
esp_timer_handle_t s_tickTimer = nullptr;

}  // namespace

bool LvglDisplay::init(LGFX* graphics) {
    if (s_initialized) {
        return true;
    }

    if (graphics == nullptr) {
        ESP_LOGE(TAG, "Cannot initialize LVGL display: graphics handle is null");
        return false;
    }

    s_gfx = graphics;
    lv_init();

    const uint32_t width = static_cast<uint32_t>(s_gfx->width());
    uint32_t drawLines = DRAW_BUFFER_LINES_DEFAULT;
    const uint32_t candidates[] = {50, 40, 30, 20, 10, DRAW_BUFFER_LINES_MIN};

    for (uint32_t lines : candidates) {
        if (lines == 0 || lines > s_gfx->height()) {
            continue;
        }
        const uint32_t pixelCountTry = width * lines;
        const size_t bufferBytesTry = pixelCountTry * sizeof(lv_color_t);
        s_bufferA = static_cast<lv_color_t*>(
            heap_caps_malloc(bufferBytesTry, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (s_bufferA == nullptr) {
            ESP_LOGW(TAG, "LVGL buffer A alloc failed (%u bytes, %u lines). Retrying smaller...",
                     static_cast<unsigned>(bufferBytesTry),
                     static_cast<unsigned>(lines));
            continue;
        }
        s_bufferB = static_cast<lv_color_t*>(
            heap_caps_malloc(bufferBytesTry, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
        if (s_bufferB == nullptr) {
            ESP_LOGW(TAG, "Draw buffer B allocation failed (%u bytes, %u lines), single buffering",
                     static_cast<unsigned>(bufferBytesTry),
                     static_cast<unsigned>(lines));
        }
        drawLines = lines;
        break;
    }

    if (s_bufferA == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LVGL draw buffer A (min %u lines)",
                 static_cast<unsigned>(DRAW_BUFFER_LINES_MIN));
        return false;
    }

    const uint32_t pixelCount = width * drawLines;
#if LV_COLOR_DEPTH != 16
    s_colorConvertBufferPixels = pixelCount;
    s_colorConvertBuffer = static_cast<uint16_t*>(
        heap_caps_malloc(s_colorConvertBufferPixels * sizeof(uint16_t), MALLOC_CAP_INTERNAL));
    if (s_colorConvertBuffer == nullptr) {
        ESP_LOGE(TAG, "Failed to allocate LVGL color conversion buffer");
        return false;
    }
#endif

    lv_disp_draw_buf_init(&s_drawBuffer, s_bufferA, s_bufferB, pixelCount);

    lv_disp_drv_init(&s_displayDriver);
    s_displayDriver.hor_res = s_gfx->width();
    s_displayDriver.ver_res = s_gfx->height();
    s_displayDriver.flush_cb = flushCallback;
    s_displayDriver.rounder_cb = rounderCallback;
    s_displayDriver.draw_buf = &s_drawBuffer;
    s_display = lv_disp_drv_register(&s_displayDriver);

    esp_timer_create_args_t tickTimerArgs = {};
    tickTimerArgs.callback = tickCallback;
    tickTimerArgs.name = "lvgl_tick";
    tickTimerArgs.dispatch_method = ESP_TIMER_TASK;

    esp_err_t timerResult = esp_timer_create(&tickTimerArgs, &s_tickTimer);
    if (timerResult != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create LVGL tick timer: %s", esp_err_to_name(timerResult));
        return false;
    }

    timerResult = esp_timer_start_periodic(s_tickTimer, LVGL_TICK_PERIOD_MS * 1000ULL);
    if (timerResult != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start LVGL tick timer: %s", esp_err_to_name(timerResult));
        return false;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "LVGL display initialized (%dx%d, %u px buffer lines)",
             s_gfx->width(), s_gfx->height(), static_cast<unsigned>(drawLines));
    return true;
}

bool LvglDisplay::isInitialized() {
    return s_initialized;
}

void LvglDisplay::taskHandler() {
    if (!s_initialized) {
        return;
    }

    lv_timer_handler();
}

void LvglDisplay::invalidateScreen() {
    if (!s_initialized || s_display == nullptr) {
        return;
    }

    lv_obj_invalidate(lv_scr_act());
}

void LvglDisplay::flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorBuffer) {
    (void)disp;
    if (s_gfx == nullptr) {
        lv_disp_flush_ready(disp);
        return;
    }

    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;
    if (width <= 0 || height <= 0) {
        lv_disp_flush_ready(disp);
        return;
    }

    s_gfx->startWrite();
#if LV_COLOR_DEPTH == 16
#if LV_COLOR_16_SWAP
    // LVGL already stores RGB565 bytes in swapped order.
    s_gfx->pushImage(
        area->x1,
        area->y1,
        width,
        height,
        reinterpret_cast<const lgfx::swap565_t*>(colorBuffer));
#else
    // LVGL stores native RGB565, pass it as plain 16-bit pixels.
    s_gfx->pushImage(
        area->x1,
        area->y1,
        width,
        height,
        reinterpret_cast<const uint16_t*>(colorBuffer));
#endif
#else
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixelCount > s_colorConvertBufferPixels || s_colorConvertBuffer == nullptr) {
        s_gfx->endWrite();
        lv_disp_flush_ready(disp);
        return;
    }

    for (size_t i = 0; i < pixelCount; ++i) {
        lv_color32_t color32 = lv_color_to32(colorBuffer[i]);
        s_colorConvertBuffer[i] = static_cast<uint16_t>(
            ((color32.ch.red & 0xF8) << 8) |
            ((color32.ch.green & 0xFC) << 3) |
            (color32.ch.blue >> 3));
    }

    s_gfx->pushImage(
        area->x1,
        area->y1,
        width,
        height,
        reinterpret_cast<lgfx::swap565_t*>(s_colorConvertBuffer));
#endif
    s_gfx->endWrite();

    lv_disp_flush_ready(disp);
}

void LvglDisplay::rounderCallback(lv_disp_drv_t* disp, lv_area_t* area) {
    (void)disp;
    // SH8601 controller path is more reliable when updates are 2-pixel aligned.
    area->x1 = (area->x1 >> 1) << 1;
    area->y1 = (area->y1 >> 1) << 1;
    area->x2 = ((area->x2 >> 1) << 1) + 1;
    area->y2 = ((area->y2 >> 1) << 1) + 1;
}

void LvglDisplay::tickCallback(void* arg) {
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}
