#include "LvglDisplay.h"
#include "hal_config.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <algorithm>
#include <memory>

namespace {

constexpr const char* TAG = "LvglDisplay";
constexpr uint32_t LVGL_TICK_PERIOD_MS = 5;

bool s_initialized = false;
lv_disp_draw_buf_t s_drawBuffer;
lv_disp_drv_t s_displayDriver;
lv_disp_t* s_display = nullptr;
lv_color_t* s_bufferA = nullptr;
lv_color_t* s_bufferB = nullptr;
DisplayFlushBackend* s_backend = nullptr;
#if LV_COLOR_DEPTH != 16
uint16_t* s_colorConvertBuffer = nullptr;
size_t s_colorConvertBufferPixels = 0;
#endif
esp_timer_handle_t s_tickTimer = nullptr;
SemaphoreHandle_t s_lvglMutex = nullptr;

lv_disp_rot_t getConfiguredDisplayRotation() {
    switch (QNOB_DISPLAY_ROTATION_DEG) {
        case 90:
            return LV_DISP_ROT_90;
        case 180:
            return LV_DISP_ROT_180;
        case 270:
            return LV_DISP_ROT_270;
        default:
            return LV_DISP_ROT_NONE;
    }
}

}  // namespace

bool LvglDisplay::init(DisplayFlushBackend* backend) {
    if (s_initialized) {
        return true;
    }

    if (backend == nullptr) {
        ESP_LOGE(TAG, "Cannot initialize LVGL display: backend handle is null");
        return false;
    }

    s_backend = backend;
    if (s_lvglMutex == nullptr) {
        s_lvglMutex = xSemaphoreCreateRecursiveMutex();
        if (s_lvglMutex == nullptr) {
            ESP_LOGE(TAG, "Failed to create LVGL recursive mutex");
            return false;
        }
    }

    lv_init();

    const uint32_t width = static_cast<uint32_t>(s_backend->width());
    const uint32_t preferredDrawLines = std::max<uint32_t>(1, s_backend->preferredDrawBufferLines());
    const uint32_t minimumDrawLines = std::max<uint32_t>(1, s_backend->minimumDrawBufferLines());
    uint32_t drawLines = preferredDrawLines;

    for (uint32_t lines = preferredDrawLines; lines >= minimumDrawLines; ) {
        if (lines > static_cast<uint32_t>(s_backend->height())) {
            if (lines == minimumDrawLines) {
                break;
            }
            lines = (lines > 10 && lines - 10 >= minimumDrawLines) ? lines - 10 : minimumDrawLines;
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
            if (lines == minimumDrawLines) {
                break;
            }
            lines = (lines > 10 && lines - 10 >= minimumDrawLines) ? lines - 10 : minimumDrawLines;
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
                 static_cast<unsigned>(minimumDrawLines));
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
    s_displayDriver.hor_res = s_backend->width();
    s_displayDriver.ver_res = s_backend->height();
    s_displayDriver.sw_rotate = (QNOB_DISPLAY_ROTATION_DEG != 0);
    s_displayDriver.flush_cb = flushCallback;
    s_displayDriver.rounder_cb = rounderCallback;
    s_displayDriver.draw_buf = &s_drawBuffer;
    s_display = lv_disp_drv_register(&s_displayDriver);
    lv_disp_set_rotation(s_display, getConfiguredDisplayRotation());

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
    ESP_LOGI(TAG,
             "LVGL display initialized (%dx%d, %u px buffer lines, backend min %u, even-align=%s, rotation=%d)",
             s_backend->width(),
             s_backend->height(),
             static_cast<unsigned>(drawLines),
             static_cast<unsigned>(minimumDrawLines),
             s_backend->requiresEvenAreaAlignment() ? "yes" : "no",
             QNOB_DISPLAY_ROTATION_DEG);
    return true;
}

bool LvglDisplay::isInitialized() {
    return s_initialized;
}

bool LvglDisplay::lock(int timeoutMs) {
    if (!s_initialized || s_lvglMutex == nullptr) {
        return false;
    }

    const TickType_t timeoutTicks =
        (timeoutMs < 0) ? portMAX_DELAY : pdMS_TO_TICKS(timeoutMs);
    return xSemaphoreTakeRecursive(s_lvglMutex, timeoutTicks) == pdTRUE;
}

void LvglDisplay::unlock() {
    if (s_initialized && s_lvglMutex != nullptr) {
        xSemaphoreGiveRecursive(s_lvglMutex);
    }
}

void LvglDisplay::taskHandler() {
    if (!s_initialized) {
        return;
    }

    if (!lock(-1)) {
        return;
    }
    lv_timer_handler();
    unlock();
}

void LvglDisplay::invalidateScreen() {
    if (!s_initialized || s_display == nullptr) {
        return;
    }

    if (!lock(-1)) {
        return;
    }
    lv_obj_invalidate(lv_scr_act());
    unlock();
}

void LvglDisplay::flushCallback(lv_disp_drv_t* disp, const lv_area_t* area, lv_color_t* colorBuffer) {
    (void)disp;
    if (s_backend == nullptr) {
        lv_disp_flush_ready(disp);
        return;
    }

    const int32_t width = area->x2 - area->x1 + 1;
    const int32_t height = area->y2 - area->y1 + 1;
    if (width <= 0 || height <= 0) {
        lv_disp_flush_ready(disp);
        return;
    }

    s_backend->beginWrite();
#if LV_COLOR_DEPTH == 16
#if LV_COLOR_16_SWAP
    s_backend->writeImage(area->x1,
                          area->y1,
                          width,
                          height,
                          colorBuffer,
                          DisplayFlushFormat::RGB565_SWAPPED);
#else
    s_backend->writeImage(area->x1,
                          area->y1,
                          width,
                          height,
                          colorBuffer,
                          DisplayFlushFormat::RGB565_NATIVE);
#endif
#else
    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    if (pixelCount > s_colorConvertBufferPixels || s_colorConvertBuffer == nullptr) {
        s_backend->endWrite();
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

    s_backend->writeImage(area->x1,
                          area->y1,
                          width,
                          height,
                          s_colorConvertBuffer,
                          DisplayFlushFormat::RGB565_SWAPPED);
#endif
    s_backend->endWrite();

    lv_disp_flush_ready(disp);
}

void LvglDisplay::rounderCallback(lv_disp_drv_t* disp, lv_area_t* area) {
    (void)disp;
    if (s_backend == nullptr || !s_backend->requiresEvenAreaAlignment()) {
        return;
    }

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
