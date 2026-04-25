#include "InitializationScreen.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>
#include <algorithm>

static const char* TAG = "InitScreen";

InitializationScreen::InitializationScreen(LGFX* graphics)
    : gfx(graphics), lvglReady(false), screenInitialized(false),
      root(nullptr), progressArc(nullptr), qLabel(nullptr), nobLabel(nullptr),
      currentArcValue(0), targetArcValue(0) {
}

int16_t InitializationScreen::normalizeAngle(float degrees) {
    float n = std::fmod(degrees, 360.0f);
    if (n < 0.0f) n += 360.0f;
    return static_cast<int16_t>(std::round(n));
}

int InitializationScreen::scalePx(int referencePx) const {
    return std::max(1, (referencePx * gfx->width()) / 240);
}

void InitializationScreen::ensureUi() {
    if (lvglReady) return;
    if (!LvglDisplay::isInitialized() && !LvglDisplay::init(gfx)) {
        ESP_LOGE(TAG, "LVGL init failed");
        return;
    }
    lvglReady = true;
    buildUi();
}

void InitializationScreen::buildUi() {
    if (root != nullptr) return;

    const int w          = gfx->width();
    const int h          = gfx->height();
    const int padding    = std::max(2, w / 32);
    const int arcDiam    = std::min(w, h) - 2 * padding;
    const int arcWidth   = std::max(12, (14 * w) / 240);
    const int discDiam   = static_cast<int>(arcDiam * 0.6f);

    // Full-screen black root
    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    // Progress arc: dark-blue track, red indicator
    progressArc = lv_arc_create(root);
    lv_obj_set_size(progressArc, arcDiam, arcDiam);
    lv_obj_center(progressArc);
    lv_obj_clear_flag(progressArc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(progressArc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_width(progressArc, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(progressArc, arcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(progressArc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(progressArc, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(progressArc, lv_color_hex(0x121A2A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(progressArc, lv_color_hex(0xFF4858), LV_PART_INDICATOR);
    lv_arc_set_mode(progressArc, LV_ARC_MODE_NORMAL);
    lv_arc_set_rotation(progressArc, 0);
    const int16_t bgStart = normalizeAngle(ARC_START_ANGLE);
    const int16_t bgEnd   = normalizeAngle(ARC_START_ANGLE + ARC_SPAN);
    lv_arc_set_bg_angles(progressArc, bgStart, bgEnd);
    lv_arc_set_angles(progressArc, bgStart, bgStart);  // empty indicator

    // Black center disc masks arc interior
    lv_obj_t* disc = lv_obj_create(root);
    lv_obj_remove_style_all(disc);
    lv_obj_set_size(disc, discDiam, discDiam);
    lv_obj_center(disc);
    lv_obj_set_style_radius(disc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(disc, LV_OPA_COVER, 0);
    lv_obj_clear_flag(disc, LV_OBJ_FLAG_SCROLLABLE);

    // Font: Montserrat 32 for AMOLED (≥400 px), 14 for smaller displays
#if defined(CONFIG_LV_FONT_MONTSERRAT_32)
    const lv_font_t* font = (w >= 400) ? &lv_font_montserrat_32 : LV_FONT_DEFAULT;
#else
    const lv_font_t* font = LV_FONT_DEFAULT;
#endif

    // "Q" label (red)
    qLabel = lv_label_create(disc);
    lv_obj_set_style_text_font(qLabel, font, 0);
    lv_obj_set_style_text_color(qLabel, lv_color_hex(0xFF0000), 0);
    lv_label_set_text(qLabel, "Q");

    // "NOB" label (white)
    nobLabel = lv_label_create(disc);
    lv_obj_set_style_text_font(nobLabel, font, 0);
    lv_obj_set_style_text_color(nobLabel, lv_color_white(), 0);
    lv_label_set_text(nobLabel, "NOB");

    // Position the two labels side-by-side in the disc center
    lv_obj_update_layout(disc);
    const int qW       = lv_obj_get_width(qLabel);
    const int nobW     = lv_obj_get_width(nobLabel);
    const int textH    = lv_obj_get_height(qLabel);
    const int totalW   = qW + nobW;
    const int discW    = lv_obj_get_width(disc);
    const int discH    = lv_obj_get_height(disc);
    lv_obj_set_pos(qLabel,   (discW - totalW) / 2,        (discH - textH) / 2);
    lv_obj_set_pos(nobLabel, (discW - totalW) / 2 + qW,   (discH - textH) / 2);

    LvglDisplay::invalidateScreen();
}

void InitializationScreen::applyArcValue(int value) {
    if (progressArc == nullptr) return;
    const int   clamped  = value < 0 ? 0 : (value > 100 ? 100 : value);
    const float endAngle = ARC_START_ANGLE + ARC_SPAN * (clamped / 100.0f);
    lv_arc_set_angles(progressArc,
                      normalizeAngle(ARC_START_ANGLE),
                      normalizeAngle(endAngle));
}

void InitializationScreen::arcAnimExec(void* var, int32_t value) {
    auto* self = static_cast<InitializationScreen*>(var);
    if (self == nullptr) return;
    self->currentArcValue = static_cast<int>(value);
    self->applyArcValue(static_cast<int>(value));
}

void InitializationScreen::setProgress(int progress) {
    const int clamped = progress < 0 ? 0 : (progress > 100 ? 100 : progress);
    ESP_LOGI(TAG, "Progress: %d", clamped);

    if (clamped == targetArcValue) return;
    targetArcValue = clamped;

    if (!lvglReady || progressArc == nullptr) return;

    lv_anim_del(this, arcAnimExec);
    if (currentArcValue == targetArcValue) return;

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, currentArcValue, targetArcValue);
    lv_anim_set_time(&anim, ANIMATION_DURATION);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, arcAnimExec);
    lv_anim_start(&anim);
}

void InitializationScreen::updateScreen() {
    ensureUi();
    if (!lvglReady) return;

    if (!screenInitialized) {
        // Render while dark to avoid visible LVGL band rendering
        gfx->setBrightness(0);
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        LvglDisplay::invalidateScreen();
        LvglDisplay::taskHandler();
        for (int b = 0; b <= 255; b += 15) {
            gfx->setBrightness(b);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        gfx->setBrightness(255);
        screenInitialized = true;

        // Start the progress animation now that LVGL is live
        if (targetArcValue > 0) {
            setProgress(targetArcValue);
        }
    }

    LvglDisplay::taskHandler();
}

void InitializationScreen::setWiFiStatus(bool /*connected*/, int /*strength*/) {
    // Reserved for future WiFi indicator
}

void InitializationScreen::setMQTTStatus(bool /*isConnected*/) {
    // Reserved for future MQTT indicator
}

void InitializationScreen::reset() {
    screenInitialized = false;
    currentArcValue   = 0;
    targetArcValue    = 0;

    if (!lvglReady) return;

    if (progressArc != nullptr) {
        lv_anim_del(this, arcAnimExec);
        applyArcValue(0);
    }
    if (root != nullptr) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
}

void InitializationScreen::hideScreen() {
    if (lvglReady && root != nullptr) {
        lv_anim_del(this, arcAnimExec);
        lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
}
