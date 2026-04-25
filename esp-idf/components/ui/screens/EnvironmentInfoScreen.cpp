#include "EnvironmentInfoScreen.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace {

constexpr const char* TAG = "EnvironmentInfoScreen";
constexpr float PI = 3.14159265358979323846f;

int16_t normalizeAngle(float degrees) {
    float normalized = std::fmod(degrees, 360.0f);
    if (normalized < 0.0f) {
        normalized += 360.0f;
    }
    return static_cast<int16_t>(std::round(normalized));
}

const lv_color_t OUTDOOR_ARC_COLOR = lv_color_hex(0x39D9FF);
const lv_color_t OUTDOOR_TRACK_COLOR = lv_color_hex(0x102238);
const lv_color_t INDOOR_TRACK_COLOR = lv_color_hex(0x153019);

}  // namespace

EnvironmentInfoScreen::EnvironmentInfoScreen(TouchPanel* touch)
    : touchPanel(touch),
      screenInitialized(false),
      pageBackRequested(false),
      deviceInfoRequested(false),
      lvglReady(false),
    ignoreNextRelease(false),
    activatedAtMs(0),
      currentDate("--/--"),
      currentTime("--:--"),
      formattedDate("-- ---"),
      currentDayOfWeek("---"),
      lastFormattedDate(""),
      lastFormattedTime(""),
      colonVisible(true),
      lastColonToggleTime(0),
      indoorTemp(18.0f),
      outdoorTemp(0.0f),
      indoorTempChanged(false),
      outdoorTempChanged(false),
      dateTimeChanged(true),
      lastActivityTime(0),
      lastUpdateTime(0),
      inactivityTimeoutReached(false),
      root(nullptr),
      outdoorTrackArc(nullptr),
    indoorTrackArc(nullptr),
    outdoorArc(nullptr),
      indoorArc(nullptr),
      centerDisc(nullptr),
      timeLabel(nullptr),
      dayLabel(nullptr),
      dateLabel(nullptr),
      indoorTempLabel(nullptr),
      outdoorTempLabel(nullptr),
      currentIndoorArcValue(0),
      currentOutdoorArcValue(0),
      targetIndoorArcValue(0),
      targetOutdoorArcValue(0),
      currentIndoorTrackValue(0),
      currentOutdoorTrackValue(0),
      animationPending(false) {
    formatDate();
    lastColonToggleTime = millis();
}

void EnvironmentInfoScreen::activate() {
    pageBackRequested = false;
    deviceInfoRequested = false;
    activatedAtMs = millis();
    ignoreNextRelease = (touchPanel != nullptr) &&
                        (touchPanel->isPressed() ||
                         touchPanel->getHasNewRelease() ||
                         touchPanel->getHasNewHoldRelease());
}

void EnvironmentInfoScreen::update() {
    const int64_t currentMillis = millis();

    // Fetch external date/time data.
    if (dateTimeCallback) {
        std::string newDate;
        std::string newTime;
        std::string newDayOfWeek;
        dateTimeCallback(newDate, newTime, newDayOfWeek);

        if (newDate != currentDate || newTime != currentTime || newDayOfWeek != currentDayOfWeek) {
            currentDate = newDate;
            currentTime = newTime;
            currentDayOfWeek = newDayOfWeek;
            formatDate();

            if (formattedDate != lastFormattedDate || currentTime != lastFormattedTime) {
                lastFormattedDate = formattedDate;
                lastFormattedTime = currentTime;
                dateTimeChanged = true;
            }
        }
    }

    // Blink the center colon once per second.
    if (currentMillis - lastColonToggleTime >= COLON_BLINK_INTERVAL) {
        colonVisible = !colonVisible;
        lastColonToggleTime = currentMillis;
        dateTimeChanged = true;
    }

    // Poll outside temperature periodically.
    if (outdoorTempCallback && (currentMillis - lastUpdateTime >= UPDATE_INTERVAL)) {
        const float newOutdoorTemp = outdoorTempCallback();
        if (std::abs(newOutdoorTemp - outdoorTemp) > 0.5f) {
            outdoorTemp = newOutdoorTemp;
            outdoorTempChanged = true;
        }
        lastUpdateTime = currentMillis;
    }

    ensureUi();
    if (lvglReady) {
        const bool forceFullRefresh = !screenInitialized;
        updateUi(forceFullRefresh);
        LvglDisplay::taskHandler();
        if (animationPending) {
            animationPending = false;
            startAnimations();
        }
    }
}

void EnvironmentInfoScreen::ensureUi() {
    if (lvglReady) {
        if (root == nullptr) {
            buildUi();
        }
        return;
    }

    if (!LvglDisplay::isInitialized() && !LvglDisplay::init()) {
        ESP_LOGE(TAG, "LVGL display init failed");
        return;
    }

    lvglReady = true;
    buildUi();
}

void EnvironmentInfoScreen::buildUi() {
    if (root != nullptr) {
        return;
    }

    const int displayW = LvglDisplay::getWidth();
    const int displayH = LvglDisplay::getHeight();
    const int displaySize = std::min(displayW, displayH);

    // Use almost full panel area on 1.75" displays while keeping tiny safety margin.
    const int framePadding = std::max(1, displaySize / 120);
    const int outerDiameter = displaySize - (2 * framePadding);
    const int outerArcWidth = std::max(scalePx(12), displaySize / 16);
    const int innerArcWidth = std::max(scalePx(9), static_cast<int>(outerArcWidth * 0.75f));
    // Requested spacing: 10% of arc thickness between outer and inner arcs.
    const int ringGap = std::max(1, innerArcWidth / 10);
    const int innerDiameter = std::max(scalePx(120), outerDiameter - 2 * (outerArcWidth + ringGap));
    // Requested spacing: 10% of inner arc thickness between inner arc and white center disc.
    const int centerGap = std::max(1, innerArcWidth / 10);
    const int centerDiameter = std::max(scalePx(150), innerDiameter - 2 * (innerArcWidth + centerGap));

    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, displayW, displayH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);

    auto createArc = [&](int diameter, int arcWidth) -> lv_obj_t* {
        lv_obj_t* arc = lv_arc_create(root);
        lv_obj_set_size(arc, diameter, diameter);
        lv_obj_center(arc);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
        lv_arc_set_rotation(arc, 0);
        lv_arc_set_bg_angles(arc,
                             normalizeAngle(ARC_START_ANGLE),
                             normalizeAngle(ARC_START_ANGLE + ARC_LENGTH));
        lv_arc_set_range(arc, 0, ARC_RANGE_MAX);
        const int16_t startAngle = normalizeAngle(ARC_START_ANGLE);
        lv_arc_set_angles(arc, startAngle, startAngle);
        return arc;
    };

    outdoorTrackArc = createArc(outerDiameter, outerArcWidth);
    lv_obj_set_style_arc_opa(outdoorTrackArc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(outdoorTrackArc, OUTDOOR_TRACK_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(outdoorTrackArc, LV_OPA_90, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(outdoorTrackArc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    outdoorArc = createArc(outerDiameter, outerArcWidth);
    lv_obj_set_style_arc_opa(outdoorArc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(outdoorArc, OUTDOOR_ARC_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(outdoorArc, OUTDOOR_ARC_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(outdoorArc, scalePx(4), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(outdoorArc, LV_OPA_50, LV_PART_INDICATOR);

    indoorTrackArc = createArc(innerDiameter, innerArcWidth);
    lv_obj_set_style_arc_opa(indoorTrackArc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(indoorTrackArc, INDOOR_TRACK_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(indoorTrackArc, LV_OPA_90, LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(indoorTrackArc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    indoorArc = createArc(innerDiameter, innerArcWidth);
    lv_obj_set_style_arc_opa(indoorArc, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(indoorArc, lv_color_hex(0x7BE85A), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(indoorArc, lv_color_hex(0x7BE85A), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(indoorArc, scalePx(4), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(indoorArc, LV_OPA_50, LV_PART_INDICATOR);

    centerDisc = lv_obj_create(root);
    lv_obj_remove_style_all(centerDisc);
    lv_obj_set_size(centerDisc, centerDiameter, centerDiameter);
    lv_obj_center(centerDisc);
    lv_obj_set_style_radius(centerDisc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(centerDisc, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(centerDisc, LV_OPA_COVER, 0);  // opaque — hides arc interior
    lv_obj_set_style_border_color(centerDisc, lv_color_hex(0x2C3948), 0);
    lv_obj_set_style_border_width(centerDisc, scalePx(2), 0);
    lv_obj_set_style_shadow_width(centerDisc, 0, 0);
    lv_obj_set_style_shadow_opa(centerDisc, LV_OPA_TRANSP, 0);
    lv_obj_clear_flag(centerDisc, LV_OBJ_FLAG_SCROLLABLE);

    // Labels are children of centerDisc so they're guaranteed above the disc and arcs.
    timeLabel = lv_label_create(centerDisc);
    lv_obj_set_style_text_color(timeLabel, lv_color_white(), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_48)
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_48, 0);
#elif defined(CONFIG_LV_FONT_MONTSERRAT_32)
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_32, 0);
#endif
    lv_label_set_text(timeLabel, "--:--");
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -scalePx(40));

    dayLabel = lv_label_create(centerDisc);
    lv_obj_set_style_text_color(dayLabel, lv_color_hex(0x9AA8BA), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_28)
    lv_obj_set_style_text_font(dayLabel, &lv_font_montserrat_28, 0);
#endif
    lv_label_set_text(dayLabel, "---");
    lv_obj_align(dayLabel, LV_ALIGN_CENTER, 0, scalePx(14));

    dateLabel = lv_label_create(centerDisc);
    lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xC7D0DD), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_28)
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_28, 0);
#endif
    lv_label_set_text(dateLabel, "-- ---");
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, scalePx(52));

    indoorTempLabel = lv_label_create(root);
    lv_obj_set_style_text_color(indoorTempLabel, lv_color_hex(0xA0FF8C), 0);
    lv_label_set_text(indoorTempLabel, "18C");

    outdoorTempLabel = lv_label_create(root);
    lv_obj_set_style_text_color(outdoorTempLabel, lv_color_hex(0x6AC7FF), 0);
    lv_label_set_text(outdoorTempLabel, "0C");

    LvglDisplay::invalidateScreen();
}

void EnvironmentInfoScreen::updateUi(bool forceFullRefresh) {
    if (!lvglReady || root == nullptr) {
        return;
    }

    if (forceFullRefresh) {
#if defined(LCD_BL_PIN) && (LCD_BL_PIN >= 0)
        // Turn off display to hide LVGL band rendering (backlight panels only).
        // AMOLED panels use display commands for brightness — dimming to 0 can
        // leave the panel off with no reliable recovery path without a BL pin.
        LvglDisplay::setBrightness(0);
#endif

        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        screenInitialized = true;

        // Set LVGL arcs to 0 (only tracks visible, no indicators)
        currentIndoorArcValue = 0;
        currentOutdoorArcValue = 0;
        applyIndoorArcFromValue(0);
        applyOutdoorArcFromValue(0);

        // Set text labels
        lv_label_set_text(timeLabel, getTimeStringForDisplay().c_str());
        lv_label_set_text(dayLabel, currentDayOfWeek.empty() ? "---" : currentDayOfWeek.c_str());
        lv_label_set_text(dateLabel, formattedDate.empty() ? "-- ---" : formattedDate.c_str());

        // Set temperature labels + arc colors
        const lv_color_t indoorColor = getTemperatureColor(indoorTemp, true);
        lv_obj_set_style_arc_color(indoorArc, indoorColor, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(indoorArc, indoorColor, LV_PART_INDICATOR);
        char tempBuffer[16];
        std::snprintf(tempBuffer, sizeof(tempBuffer), "IN %dC", static_cast<int>(std::lround(indoorTemp)));
        lv_label_set_text(indoorTempLabel, tempBuffer);
        lv_obj_set_style_text_color(indoorTempLabel, indoorColor, 0);

        const lv_color_t outdoorColor = getTemperatureColor(outdoorTemp, false);
        std::snprintf(tempBuffer, sizeof(tempBuffer), "OUT %dC", static_cast<int>(std::lround(outdoorTemp)));
        lv_label_set_text(outdoorTempLabel, tempBuffer);
        lv_obj_set_style_text_color(outdoorTempLabel, outdoorColor, 0);

        // Force LVGL layout so label sizes are valid for transform positioning.
        lv_obj_update_layout(root);

        positionTemperatureLabels();

        // Mark frame dirty; let the normal per-frame handler render it in the
        // outer update loop to avoid a long blocking render in this call.
        LvglDisplay::invalidateScreen();

#if defined(LCD_BL_PIN) && (LCD_BL_PIN >= 0)
        // Fade display in — user sees text + empty tracks instantly
        for (int b = 0; b <= 255; b += 15) {
            LvglDisplay::setBrightness(static_cast<uint8_t>(b));
            vTaskDelay(pdMS_TO_TICKS(2));
        }
#endif
        LvglDisplay::setBrightness(255);

        // Schedule arc grow animations to start after the rendered frame is flushed.
        animationPending = true;

        dateTimeChanged = false;
        indoorTempChanged = false;
        outdoorTempChanged = false;
        return;
    }

    // Incremental updates (not during initial animation)
    const float indoorNormalized = constrain((indoorTemp - 0.0f) / 40.0f, 0.0f, 1.0f);
    const float outdoorNormalized = constrain((outdoorTemp - (-20.0f)) / 70.0f, 0.0f, 1.0f);
    const int indoorValue = static_cast<int>(std::lround(indoorNormalized * ARC_RANGE_MAX));
    const int outdoorValue = static_cast<int>(std::lround(outdoorNormalized * ARC_RANGE_MAX));

    if (indoorTempChanged) {
        const lv_color_t indoorColor = getTemperatureColor(indoorTemp, true);
        lv_obj_set_style_arc_color(indoorArc, indoorColor, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(indoorArc, indoorColor, LV_PART_INDICATOR);

        char tempBuffer[16];
        std::snprintf(tempBuffer, sizeof(tempBuffer), "IN %dC", static_cast<int>(std::lround(indoorTemp)));
        lv_label_set_text(indoorTempLabel, tempBuffer);
        lv_obj_set_style_text_color(indoorTempLabel, indoorColor, 0);

        startIndoorArcAnimation(indoorValue, false);
        targetIndoorArcValue = indoorValue;
        positionTemperatureLabels();
    }

    if (outdoorTempChanged) {
        const lv_color_t outdoorColor = getTemperatureColor(outdoorTemp, false);
        char tempBuffer[16];
        std::snprintf(tempBuffer, sizeof(tempBuffer), "OUT %dC", static_cast<int>(std::lround(outdoorTemp)));
        lv_label_set_text(outdoorTempLabel, tempBuffer);
        lv_obj_set_style_text_color(outdoorTempLabel, outdoorColor, 0);

        startOutdoorArcAnimation(outdoorValue, false);
        targetOutdoorArcValue = outdoorValue;
        positionTemperatureLabels();
    }

    if (dateTimeChanged) {
        lv_label_set_text(timeLabel, getTimeStringForDisplay().c_str());
        lv_label_set_text(dayLabel, currentDayOfWeek.empty() ? "---" : currentDayOfWeek.c_str());
        lv_label_set_text(dateLabel, formattedDate.empty() ? "-- ---" : formattedDate.c_str());
    }

    dateTimeChanged = false;
    indoorTempChanged = false;
    outdoorTempChanged = false;
}

void EnvironmentInfoScreen::startAnimations() {
    const float indoorNorm  = constrain((indoorTemp - 0.0f)   / 40.0f, 0.0f, 1.0f);
    const float outdoorNorm = constrain((outdoorTemp - (-20.0f)) / 70.0f, 0.0f, 1.0f);
    const int indoorValue   = static_cast<int>(std::lround(indoorNorm  * ARC_RANGE_MAX));
    const int outdoorValue  = static_cast<int>(std::lround(outdoorNorm * ARC_RANGE_MAX));

    ESP_LOGI(TAG, "startAnimations: indoor=%d outdoor=%d", indoorValue, outdoorValue);

    // Foreground arcs: 0 → temperature value
    startOutdoorArcAnimation(outdoorValue, false, 0);
    targetOutdoorArcValue = outdoorValue;

    // Start both arcs together so intro timing matches.
    startIndoorArcAnimation(indoorValue, false, 0);
    targetIndoorArcValue = indoorValue;

    // Track arcs: 0 → full span, same duration so backgrounds grow in sync with foreground
    startOutdoorTrackAnimation(ARC_RANGE_MAX, false, 0);
    startIndoorTrackAnimation(ARC_RANGE_MAX, false, 0);

    positionTemperatureLabels();
}

void EnvironmentInfoScreen::startIndoorArcAnimation(int targetValue, bool immediate, uint32_t delayMs) {
    lv_anim_del(this, indoorArcAnimExec);

    if (immediate) {
        applyIndoorArcFromValue(targetValue);
        currentIndoorArcValue = targetValue;
        return;
    }

    const int startValue = currentIndoorArcValue;
    if (startValue == targetValue) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, startValue, targetValue);
    lv_anim_set_time(&anim, ANIMATION_DURATION);
    lv_anim_set_delay(&anim, delayMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, indoorArcAnimExec);
    lv_anim_start(&anim);
}

void EnvironmentInfoScreen::startOutdoorArcAnimation(int targetValue, bool immediate, uint32_t delayMs) {
    lv_anim_del(this, outdoorArcAnimExec);

    if (immediate) {
        currentOutdoorArcValue = targetValue;
        applyOutdoorArcFromValue(currentOutdoorArcValue);
        return;
    }

    if (currentOutdoorArcValue == targetValue && delayMs == 0) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, currentOutdoorArcValue, targetValue);
    lv_anim_set_time(&anim, ANIMATION_DURATION);
    lv_anim_set_delay(&anim, delayMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, outdoorArcAnimExec);
    lv_anim_start(&anim);
}

void EnvironmentInfoScreen::indoorArcAnimExec(void* var, int32_t value) {
    EnvironmentInfoScreen* self = static_cast<EnvironmentInfoScreen*>(var);
    if (self == nullptr) {
        return;
    }

    self->currentIndoorArcValue = static_cast<int>(value);
    self->applyIndoorArcFromValue(static_cast<int>(value));
}

void EnvironmentInfoScreen::outdoorArcAnimExec(void* var, int32_t value) {
    EnvironmentInfoScreen* self = static_cast<EnvironmentInfoScreen*>(var);
    if (self == nullptr) {
        return;
    }

    self->currentOutdoorArcValue = static_cast<int>(value);
    self->applyOutdoorArcFromValue(self->currentOutdoorArcValue);
}

void EnvironmentInfoScreen::startIndoorTrackAnimation(int targetValue, bool immediate, uint32_t delayMs) {
    lv_anim_del(this, indoorTrackAnimExec);

    if (immediate) {
        currentIndoorTrackValue = targetValue;
        applyIndoorTrackFromValue(targetValue);
        return;
    }

    if (currentIndoorTrackValue == targetValue && delayMs == 0) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, currentIndoorTrackValue, targetValue);
    lv_anim_set_time(&anim, ANIMATION_DURATION);
    lv_anim_set_delay(&anim, delayMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, indoorTrackAnimExec);
    lv_anim_start(&anim);
}

void EnvironmentInfoScreen::startOutdoorTrackAnimation(int targetValue, bool immediate, uint32_t delayMs) {
    lv_anim_del(this, outdoorTrackAnimExec);

    if (immediate) {
        currentOutdoorTrackValue = targetValue;
        applyOutdoorTrackFromValue(targetValue);
        return;
    }

    if (currentOutdoorTrackValue == targetValue && delayMs == 0) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, currentOutdoorTrackValue, targetValue);
    lv_anim_set_time(&anim, ANIMATION_DURATION);
    lv_anim_set_delay(&anim, delayMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_in_out);
    lv_anim_set_exec_cb(&anim, outdoorTrackAnimExec);
    lv_anim_start(&anim);
}

void EnvironmentInfoScreen::indoorTrackAnimExec(void* var, int32_t value) {
    EnvironmentInfoScreen* self = static_cast<EnvironmentInfoScreen*>(var);
    if (self == nullptr) {
        return;
    }

    self->currentIndoorTrackValue = static_cast<int>(value);
    self->applyIndoorTrackFromValue(static_cast<int>(value));
}

void EnvironmentInfoScreen::outdoorTrackAnimExec(void* var, int32_t value) {
    EnvironmentInfoScreen* self = static_cast<EnvironmentInfoScreen*>(var);
    if (self == nullptr) {
        return;
    }

    self->currentOutdoorTrackValue = static_cast<int>(value);
    self->applyOutdoorTrackFromValue(static_cast<int>(value));
}

void EnvironmentInfoScreen::positionTemperatureLabels() {
    if (outdoorTrackArc == nullptr || indoorArc == nullptr ||
        indoorTempLabel == nullptr || outdoorTempLabel == nullptr) {
        return;
    }

    const int centerX = LvglDisplay::getWidth() / 2;
    const int centerY = LvglDisplay::getHeight() / 2;
    const int indoorRadius  = lv_obj_get_width(indoorArc) / 2;
    const int outdoorRadius = lv_obj_get_width(outdoorTrackArc) / 2;

    // Compute the angle (degrees, clockwise from east = 0°) at the tip of each arc.
    const float indoorNorm  = constrain((indoorTemp  - 0.0f)    / 40.0f,  0.0f, 1.0f);
    const float outdoorNorm = constrain((outdoorTemp - (-20.0f)) / 70.0f, 0.0f, 1.0f);
    const float indoorEndAngle  = ARC_START_ANGLE + ARC_LENGTH * indoorNorm;
    const float outdoorEndAngle = ARC_START_ANGLE + ARC_LENGTH * outdoorNorm;

    // Place a label centered on the arc tip and rotate it tangentially so it
    // reads naturally along the arc direction.
    auto placeArcTipLabel = [&](lv_obj_t* label, int radius, float tipAngleDeg) {
        const float angleRad = tipAngleDeg * PI / 180.0f;
        const int tipX = centerX + static_cast<int>(std::round(radius * std::cos(angleRad)));
        const int tipY = centerY + static_cast<int>(std::round(radius * std::sin(angleRad)));

        const int w = lv_obj_get_width(label);
        const int h = lv_obj_get_height(label);
        lv_obj_set_pos(label, tipX - w / 2, tipY - h / 2);

        // Rotate text tangentially (perpendicular to the radius).
        // Use (tipAngle + 90°) as the tangent direction; flip by 180° if it
        // would produce upside-down text (tangent angle in the 90°–270° range).
        float textAngle = tipAngleDeg + 90.0f;
        if (textAngle >= 90.0f && textAngle < 270.0f) {
            textAngle += 180.0f;
        }
        textAngle = std::fmod(textAngle + 360.0f, 360.0f);

        lv_obj_set_style_transform_pivot_x(label, w / 2, 0);
        lv_obj_set_style_transform_pivot_y(label, h / 2, 0);
        lv_obj_set_style_transform_angle(label, static_cast<int16_t>(textAngle * 10.0f), 0);
        lv_obj_add_flag(label, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
    };

    placeArcTipLabel(indoorTempLabel,  indoorRadius,  indoorEndAngle);
    placeArcTipLabel(outdoorTempLabel, outdoorRadius, outdoorEndAngle);
}

void EnvironmentInfoScreen::applyIndoorArcFromValue(int value) {
    if (indoorArc == nullptr) {
        return;
    }

    const int clampedValue = constrain(value, 0, ARC_RANGE_MAX);
    const float progress = static_cast<float>(clampedValue) / static_cast<float>(ARC_RANGE_MAX);
    const float endAngleFloat = ARC_START_ANGLE + (ARC_LENGTH * progress);
    const int16_t startAngle = normalizeAngle(ARC_START_ANGLE);
    const int16_t endAngle = normalizeAngle(endAngleFloat);

    lv_arc_set_angles(indoorArc, startAngle, endAngle);
}

void EnvironmentInfoScreen::applyIndoorTrackFromValue(int value) {
    if (indoorTrackArc == nullptr) {
        return;
    }

    const int clampedValue = constrain(value, 0, ARC_RANGE_MAX);
    const float progress = static_cast<float>(clampedValue) / static_cast<float>(ARC_RANGE_MAX);
    const float endAngleFloat = ARC_START_ANGLE + (ARC_LENGTH * progress);
    const int16_t startAngle = normalizeAngle(ARC_START_ANGLE);
    const int16_t endAngle = normalizeAngle(endAngleFloat);

    lv_arc_set_angles(indoorTrackArc, startAngle, endAngle);
}

void EnvironmentInfoScreen::applyOutdoorArcFromValue(int value) {
    if (outdoorArc == nullptr) {
        return;
    }

    const int clampedValue = constrain(value, 0, ARC_RANGE_MAX);
    const float progress = static_cast<float>(clampedValue) / static_cast<float>(ARC_RANGE_MAX);
    const float endAngleFloat = ARC_START_ANGLE + (ARC_LENGTH * progress);
    const int16_t startAngle = normalizeAngle(ARC_START_ANGLE);
    const int16_t endAngle = normalizeAngle(endAngleFloat);

    lv_arc_set_angles(outdoorArc, startAngle, endAngle);
}

void EnvironmentInfoScreen::applyOutdoorTrackFromValue(int value) {
    if (outdoorTrackArc == nullptr) {
        return;
    }

    const int clampedValue = constrain(value, 0, ARC_RANGE_MAX);
    const float progress = static_cast<float>(clampedValue) / static_cast<float>(ARC_RANGE_MAX);
    const float endAngleFloat = ARC_START_ANGLE + (ARC_LENGTH * progress);
    const int16_t startAngle = normalizeAngle(ARC_START_ANGLE);
    const int16_t endAngle = normalizeAngle(endAngleFloat);

    lv_arc_set_angles(outdoorTrackArc, startAngle, endAngle);
}

lv_color_t EnvironmentInfoScreen::getTemperatureColor(float temperature, bool isIndoor) {
    auto lerp = [](uint8_t from, uint8_t to, float t) -> uint8_t {
        return static_cast<uint8_t>(from + (to - from) * t);
    };

    uint8_t r = 124;
    uint8_t g = 230;
    uint8_t b = 90;

    if (isIndoor) {
        if (temperature <= 18.0f) {
            const float t = constrain((temperature - 10.0f) / 8.0f, 0.0f, 1.0f);
            r = lerp(95, 124, t);
            g = lerp(210, 230, t);
            b = lerp(140, 90, t);
        } else if (temperature <= 24.0f) {
            r = 124;
            g = 230;
            b = 90;
        } else if (temperature <= 28.0f) {
            const float t = constrain((temperature - 24.0f) / 4.0f, 0.0f, 1.0f);
            r = lerp(124, 255, t);
            g = lerp(230, 191, t);
            b = lerp(90, 74, t);
        } else {
            const float t = constrain((temperature - 28.0f) / 8.0f, 0.0f, 1.0f);
            r = 255;
            g = lerp(191, 64, t);
            b = lerp(74, 80, t);
        }
    } else {
        if (temperature <= 0.0f) {
            r = 68;
            g = 214;
            b = 255;
        } else if (temperature <= 15.0f) {
            const float t = constrain(temperature / 15.0f, 0.0f, 1.0f);
            r = lerp(68, 124, t);
            g = lerp(214, 230, t);
            b = lerp(255, 90, t);
        } else if (temperature <= 28.0f) {
            const float t = constrain((temperature - 15.0f) / 13.0f, 0.0f, 1.0f);
            r = lerp(124, 255, t);
            g = lerp(230, 191, t);
            b = lerp(90, 74, t);
        } else {
            const float t = constrain((temperature - 28.0f) / 12.0f, 0.0f, 1.0f);
            r = 255;
            g = lerp(191, 64, t);
            b = lerp(74, 80, t);
        }
    }

    return lv_color_make(r, g, b);
}

std::string EnvironmentInfoScreen::getTimeStringForDisplay() const {
    std::string displayTime = currentTime.empty() ? "--:--" : currentTime;
    size_t colonPos = displayTime.find(':');
    if (!colonVisible && colonPos != std::string::npos) {
        displayTime[colonPos] = ' ';
    }
    return displayTime;
}

int EnvironmentInfoScreen::scalePx(int referencePx) const {
    return std::max(1, (referencePx * LvglDisplay::getWidth()) / 240);
}

void EnvironmentInfoScreen::formatDate() {
    size_t firstSlash = currentDate.find('/');
    size_t secondSlash = currentDate.find('/', firstSlash == std::string::npos ? 0 : firstSlash + 1);

    if (firstSlash != std::string::npos && secondSlash != std::string::npos) {
        std::string monthStr = currentDate.substr(firstSlash + 1, secondSlash - firstSlash - 1);
        std::string dayStr = currentDate.substr(secondSlash + 1);

        bool isValidMonth = !monthStr.empty() && std::all_of(monthStr.begin(), monthStr.end(), ::isdigit);
        bool isValidDay = !dayStr.empty() && std::all_of(dayStr.begin(), dayStr.end(), ::isdigit);

        if (isValidMonth && isValidDay) {
            while (dayStr.length() > 1 && dayStr[0] == '0') {
                dayStr.erase(dayStr.begin());
            }

            int monthNum = std::stoi(monthStr);
            std::string monthName = getMonthName(monthNum);
            formattedDate = dayStr + " " + monthName;
            return;
        }
    }

    formattedDate = "-- ---";
}

std::string EnvironmentInfoScreen::getMonthName(int month) {
    static constexpr const char* MONTHS[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };

    if (month >= 1 && month <= 12) {
        return MONTHS[month - 1];
    }
    return "---";
}

void EnvironmentInfoScreen::updateDateTime(const std::string& date, const std::string& time, const std::string& dayOfWeek) {
    if (date != currentDate || time != currentTime || (!dayOfWeek.empty() && dayOfWeek != currentDayOfWeek)) {
        currentDate = date;
        currentTime = time;
        if (!dayOfWeek.empty()) {
            currentDayOfWeek = dayOfWeek;
        }
        formatDate();
        dateTimeChanged = true;
    }
}

void EnvironmentInfoScreen::updateIndoorTemperature(float temperature) {
    if (std::abs(temperature - indoorTemp) > 0.5f) {
        indoorTemp = temperature;
        indoorTempChanged = true;
    }
}

void EnvironmentInfoScreen::updateOutdoorTemperature(float temperature) {
    if (std::abs(temperature - outdoorTemp) > 0.5f) {
        outdoorTemp = temperature;
        outdoorTempChanged = true;
    }
}

void EnvironmentInfoScreen::resetLastActivityTime() {
    lastActivityTime = millis();
    inactivityTimeoutReached = false;
}

bool EnvironmentInfoScreen::isInactivityTimeoutReached() {
    if ((millis() - lastActivityTime) > INACTIVITY_TIMEOUT) {
        inactivityTimeoutReached = true;
    }
    return inactivityTimeoutReached;
}

bool EnvironmentInfoScreen::isPageBackRequested() {
    return pageBackRequested;
}

void EnvironmentInfoScreen::resetPageBackRequest() {
    pageBackRequested = false;
}

bool EnvironmentInfoScreen::isDeviceInfoRequested() {
    return deviceInfoRequested;
}

void EnvironmentInfoScreen::resetDeviceInfoRequest() {
    deviceInfoRequested = false;
}

void EnvironmentInfoScreen::deactivate() {
    screenInitialized = false;
    animationPending = false;
    lv_anim_del(this, indoorArcAnimExec);
    lv_anim_del(this, outdoorArcAnimExec);
    lv_anim_del(this, indoorTrackAnimExec);
    lv_anim_del(this, outdoorTrackAnimExec);
    if (root != nullptr) {
        if (currentIndoorArcValue > 0 || currentOutdoorArcValue > 0 ||
            currentIndoorTrackValue > 0 || currentOutdoorTrackValue > 0) {
            startIndoorArcAnimation(0, false, 0);
            startOutdoorArcAnimation(0, false, 0);
            startIndoorTrackAnimation(0, false, 0);
            startOutdoorTrackAnimation(0, false, 0);

            const int64_t tStart = millis();
            while ((millis() - tStart) < ANIMATION_DURATION) {
                LvglDisplay::taskHandler();
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }

        currentIndoorArcValue = 0;
        currentOutdoorArcValue = 0;
        currentIndoorTrackValue = 0;
        currentOutdoorTrackValue = 0;
        applyIndoorArcFromValue(0);
        applyOutdoorArcFromValue(0);
        applyIndoorTrackFromValue(0);
        applyOutdoorTrackFromValue(0);
        LvglDisplay::invalidateScreen();
        LvglDisplay::taskHandler();
        lv_obj_del(root);
        root = nullptr;
        outdoorTrackArc = nullptr;
        indoorTrackArc = nullptr;
        outdoorArc = nullptr;
        indoorArc = nullptr;
        centerDisc = nullptr;
        timeLabel = nullptr;
        dayLabel = nullptr;
        dateLabel = nullptr;
        indoorTempLabel = nullptr;
        outdoorTempLabel = nullptr;
    }
}

void EnvironmentInfoScreen::resetScreen() {
    ESP_LOGI(TAG, "resetScreen called - forcing redraw");
    screenInitialized = false;
    animationPending = false;
    dateTimeChanged = true;
    indoorTempChanged = true;
    outdoorTempChanged = true;
    lv_anim_del(this, indoorArcAnimExec);
    lv_anim_del(this, outdoorArcAnimExec);
    lv_anim_del(this, indoorTrackAnimExec);
    lv_anim_del(this, outdoorTrackAnimExec);

    if (root != nullptr) {
        lv_obj_del(root);
        root = nullptr;
        outdoorTrackArc = nullptr;
        indoorTrackArc = nullptr;
        outdoorArc = nullptr;
        indoorArc = nullptr;
        centerDisc = nullptr;
        timeLabel = nullptr;
        dayLabel = nullptr;
        dateLabel = nullptr;
        indoorTempLabel = nullptr;
        outdoorTempLabel = nullptr;
    }

    currentIndoorArcValue = 0;
    currentOutdoorArcValue = 0;
    currentIndoorTrackValue = 0;
    currentOutdoorTrackValue = 0;

    activate();
    LvglDisplay::invalidateScreen();
}

