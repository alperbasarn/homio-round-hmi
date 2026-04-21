#include "EnvironmentInfoScreen.h"
#include "LvglDisplay.h"
#include "esp_log.h"
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

const std::array<lv_color_t, 4> OUTDOOR_SEGMENT_COLORS = {
    lv_color_hex(0xFF3A55),
    lv_color_hex(0xFF7B39),
    lv_color_hex(0xFFD85C),
    lv_color_hex(0x39D9FF)
};

}  // namespace

EnvironmentInfoScreen::EnvironmentInfoScreen(LGFX* graphics, TouchPanel* touch)
    : gfx(graphics),
      touchPanel(touch),
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
      indoorArc(nullptr),
      outdoorSegmentArcs{nullptr, nullptr, nullptr, nullptr},
      centerDisc(nullptr),
      timeLabel(nullptr),
      dayLabel(nullptr),
      dateLabel(nullptr),
      indoorTempLabel(nullptr),
      outdoorTempLabel(nullptr),
      outdoorSegmentStops{0.0f, 0.25f, 0.50f, 0.75f, 1.0f},
      outdoorSegmentStartAngles{0, 0, 0, 0},
      outdoorSegmentEndAngles{0, 0, 0, 0},
      currentIndoorArcValue(0),
      currentOutdoorArcValue(0),
      targetIndoorArcValue(0),
      targetOutdoorArcValue(0) {
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
    }

    // Handle touch gestures
    if (touchPanel && touchPanel->getHasNewGesture()) {
        resetLastActivityTime();
        touch_gesture_t touchGesture = touchPanel->getLastGesture();

        if (touchGesture == GESTURE_SWIPE_RIGHT) {
            pageBackRequested = true;
            screenInitialized = false;
            lv_anim_del(this, indoorArcAnimExec);
            lv_anim_del(this, outdoorArcAnimExec);
            if (root) {
                lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
            }
            ESP_LOGI(TAG, "Swipe right detected, navigating to mode controller");
        } else if (touchGesture == GESTURE_SWIPE_UP) {
            deviceInfoRequested = true;
            screenInitialized = false;
            lv_anim_del(this, indoorArcAnimExec);
            lv_anim_del(this, outdoorArcAnimExec);
            if (root) {
                lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
            }
            ESP_LOGI(TAG, "Swipe up detected, navigating to device info");
        }
    }

    if (touchPanel && touchPanel->getHasNewRelease()) {
        const int64_t elapsedSinceActivation = millis() - activatedAtMs;
        if (ignoreNextRelease || elapsedSinceActivation < STALE_RELEASE_GUARD_MS) {
            ignoreNextRelease = false;
            ESP_LOGI(TAG, "Ignoring stale release after screen activation (%lld ms)", elapsedSinceActivation);
        } else {
            resetLastActivityTime();
            pageBackRequested = true;
            screenInitialized = false;
            lv_anim_del(this, indoorArcAnimExec);
            lv_anim_del(this, outdoorArcAnimExec);
            if (root) {
                lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
            }
            ESP_LOGI(TAG, "Tap release detected, navigating to mode controller");
        }
    } else if (ignoreNextRelease && touchPanel && !touchPanel->isPressed() &&
               (millis() - activatedAtMs) >= STALE_RELEASE_GUARD_MS) {
        ignoreNextRelease = false;
    }
}

void EnvironmentInfoScreen::ensureUi() {
    if (lvglReady) {
        return;
    }

    if (!LvglDisplay::isInitialized() && !LvglDisplay::init(gfx)) {
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

    const int displayW = gfx->width();
    const int displayH = gfx->height();
    const int displaySize = std::min(displayW, displayH);

    const int framePadding = std::max(scalePx(10), displaySize / 20);
    const int outerDiameter = displaySize - (2 * framePadding);
    const int outerArcWidth = std::max(scalePx(12), displaySize / 18);
    const int ringGap = std::max(scalePx(4), displaySize / 58);
    const int innerArcWidth = std::max(scalePx(9), outerArcWidth - scalePx(8));
    const int innerDiameter = std::max(scalePx(120), outerDiameter - 2 * (outerArcWidth + ringGap));
    const int centerDiameter = std::max(scalePx(90), innerDiameter - 2 * (innerArcWidth + scalePx(8)));

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
    lv_obj_set_style_arc_color(outdoorTrackArc, lv_color_hex(0x102238), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(outdoorTrackArc, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(outdoorTrackArc, LV_OPA_TRANSP, LV_PART_INDICATOR);

    const float segmentSpan = ARC_LENGTH / static_cast<float>(OUTDOOR_SEGMENT_COUNT);
    const float segmentGap = (displayW >= 400) ? 4.0f : 3.0f;

    for (size_t i = 0; i < OUTDOOR_SEGMENT_COUNT; ++i) {
        lv_obj_t* segmentArc = createArc(outerDiameter, outerArcWidth);
        lv_obj_set_style_arc_opa(segmentArc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_color(segmentArc, OUTDOOR_SEGMENT_COLORS[i], LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(segmentArc, OUTDOOR_SEGMENT_COLORS[i], LV_PART_INDICATOR);
        lv_obj_set_style_shadow_width(segmentArc, scalePx(4), LV_PART_INDICATOR);
        lv_obj_set_style_shadow_opa(segmentArc, LV_OPA_50, LV_PART_INDICATOR);

        const float segStart = (segmentSpan * static_cast<float>(i)) + (i == 0 ? 0.0f : segmentGap * 0.5f);
        const float segEnd = (segmentSpan * static_cast<float>(i + 1))
                           - (i == OUTDOOR_SEGMENT_COUNT - 1 ? 0.0f : segmentGap * 0.5f);
        lv_arc_set_bg_angles(segmentArc,
                             normalizeAngle(ARC_START_ANGLE + segStart),
                             normalizeAngle(ARC_START_ANGLE + segEnd));

        outdoorSegmentArcs[i] = segmentArc;
        outdoorSegmentStartAngles[i] = normalizeAngle(ARC_START_ANGLE + segStart);
        outdoorSegmentEndAngles[i] = normalizeAngle(ARC_START_ANGLE + segEnd);
    }

    indoorArc = createArc(innerDiameter, innerArcWidth);
    lv_obj_set_style_arc_color(indoorArc, lv_color_hex(0x153019), LV_PART_MAIN);
    lv_obj_set_style_arc_opa(indoorArc, LV_OPA_90, LV_PART_MAIN);
    lv_obj_set_style_arc_color(indoorArc, lv_color_hex(0x7BE85A), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_color(indoorArc, lv_color_hex(0x7BE85A), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_width(indoorArc, scalePx(4), LV_PART_INDICATOR);
    lv_obj_set_style_shadow_opa(indoorArc, LV_OPA_50, LV_PART_INDICATOR);

    centerDisc = lv_obj_create(root);
    lv_obj_remove_style_all(centerDisc);
    lv_obj_set_size(centerDisc, centerDiameter, centerDiameter);
    lv_obj_center(centerDisc);
    lv_obj_set_style_radius(centerDisc, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(centerDisc, lv_color_hex(0x0A1016), 0);
    lv_obj_set_style_bg_grad_color(centerDisc, lv_color_hex(0x161E2A), 0);
    lv_obj_set_style_bg_grad_dir(centerDisc, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(centerDisc, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(centerDisc, lv_color_hex(0x2C3948), 0);
    lv_obj_set_style_border_width(centerDisc, scalePx(2), 0);
    lv_obj_set_style_shadow_color(centerDisc, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_width(centerDisc, scalePx(5), 0);
    lv_obj_set_style_shadow_opa(centerDisc, LV_OPA_60, 0);
    lv_obj_clear_flag(centerDisc, LV_OBJ_FLAG_SCROLLABLE);

    timeLabel = lv_label_create(centerDisc);
    lv_obj_set_style_text_color(timeLabel, lv_color_white(), 0);
    lv_label_set_text(timeLabel, "--:--");
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, -scalePx(20));

    dayLabel = lv_label_create(centerDisc);
    lv_obj_set_style_text_color(dayLabel, lv_color_hex(0x9AA8BA), 0);
    lv_label_set_text(dayLabel, "---");
    lv_obj_align(dayLabel, LV_ALIGN_CENTER, 0, scalePx(4));

    dateLabel = lv_label_create(centerDisc);
    lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xC7D0DD), 0);
    lv_label_set_text(dateLabel, "-- ---");
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, scalePx(22));

    indoorTempLabel = lv_label_create(root);
    lv_obj_set_style_text_color(indoorTempLabel, lv_color_hex(0xA0FF8C), 0);
    lv_label_set_text(indoorTempLabel, "18C");

    outdoorTempLabel = lv_label_create(root);
    lv_obj_set_style_text_color(outdoorTempLabel, lv_color_hex(0x6AC7FF), 0);
    lv_label_set_text(outdoorTempLabel, "0C");

    positionTemperatureLabels();
    LvglDisplay::invalidateScreen();
}

void EnvironmentInfoScreen::updateUi(bool forceFullRefresh) {
    if (!lvglReady || root == nullptr) {
        return;
    }

    if (forceFullRefresh) {
        // Turn off display to hide LVGL band rendering
        gfx->setBrightness(0);

        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        screenInitialized = true;

        // Set LVGL arcs to 0 (only tracks visible, no indicators)
        currentIndoorArcValue = 0;
        currentOutdoorArcValue = 0;
        applyIndoorArcFromValue(0);
        updateOutdoorSegmentsFromValue(0);

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

        positionTemperatureLabels();

        // Render full LVGL frame while display is dark (text + tracks, arcs at 0)
        LvglDisplay::invalidateScreen();
        LvglDisplay::taskHandler();

        // Fade display in — user sees text + empty tracks instantly
        for (int b = 0; b <= 255; b += 15) {
            gfx->setBrightness(b);
            vTaskDelay(pdMS_TO_TICKS(2));
        }
        gfx->setBrightness(255);

        // Compute animation targets
        const float indoorNorm = constrain((indoorTemp - 0.0f) / 40.0f, 0.0f, 1.0f);
        const float outdoorNorm = constrain((outdoorTemp - (-20.0f)) / 70.0f, 0.0f, 1.0f);
        const int indoorValue = static_cast<int>(std::lround(indoorNorm * ARC_RANGE_MAX));
        const int outdoorValue = static_cast<int>(std::lround(outdoorNorm * ARC_RANGE_MAX));
        ESP_LOGI(TAG, "ENV_ARC_V4 start=%d indoor=%d outdoor=%d",
                 normalizeAngle(ARC_START_ANGLE), indoorValue, outdoorValue);

        startIndoorArcAnimation(indoorValue, false);
        startOutdoorArcAnimation(outdoorValue, false);
        targetIndoorArcValue = indoorValue;
        targetOutdoorArcValue = outdoorValue;

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
    }

    if (outdoorTempChanged) {
        const lv_color_t outdoorColor = getTemperatureColor(outdoorTemp, false);
        char tempBuffer[16];
        std::snprintf(tempBuffer, sizeof(tempBuffer), "OUT %dC", static_cast<int>(std::lround(outdoorTemp)));
        lv_label_set_text(outdoorTempLabel, tempBuffer);
        lv_obj_set_style_text_color(outdoorTempLabel, outdoorColor, 0);

        startOutdoorArcAnimation(outdoorValue, false);
        targetOutdoorArcValue = outdoorValue;
    }

    if (dateTimeChanged) {
        lv_label_set_text(timeLabel, getTimeStringForDisplay().c_str());
        lv_label_set_text(dayLabel, currentDayOfWeek.empty() ? "---" : currentDayOfWeek.c_str());
        lv_label_set_text(dateLabel, formattedDate.empty() ? "-- ---" : formattedDate.c_str());
    }

    positionTemperatureLabels();

    dateTimeChanged = false;
    indoorTempChanged = false;
    outdoorTempChanged = false;
}

void EnvironmentInfoScreen::startIndoorArcAnimation(int targetValue, bool immediate) {
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
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, indoorArcAnimExec);
    lv_anim_start(&anim);
}

void EnvironmentInfoScreen::startOutdoorArcAnimation(int targetValue, bool immediate) {
    lv_anim_del(this, outdoorArcAnimExec);

    if (immediate) {
        currentOutdoorArcValue = targetValue;
        updateOutdoorSegmentsFromValue(currentOutdoorArcValue);
        return;
    }

    if (currentOutdoorArcValue == targetValue) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, currentOutdoorArcValue, targetValue);
    lv_anim_set_time(&anim, ANIMATION_DURATION);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
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
    self->updateOutdoorSegmentsFromValue(self->currentOutdoorArcValue);
}

void EnvironmentInfoScreen::positionTemperatureLabels() {
    if (outdoorTrackArc == nullptr || indoorArc == nullptr || indoorTempLabel == nullptr || outdoorTempLabel == nullptr) {
        return;
    }

    const int centerX = gfx->width() / 2;
    const int centerY = gfx->height() / 2;
    const int indoorRadius = lv_obj_get_width(indoorArc) / 2;
    const int outdoorRadius = lv_obj_get_width(outdoorTrackArc) / 2;

    auto placeLabel = [&](lv_obj_t* label, int radius, float angleDeg) {
        const float angleRad = angleDeg * PI / 180.0f;
        const int x = centerX + static_cast<int>(std::round(radius * std::cos(angleRad)));
        const int y = centerY + static_cast<int>(std::round(radius * std::sin(angleRad)));
        const int labelW = lv_obj_get_width(label);
        const int labelH = lv_obj_get_height(label);
        lv_obj_set_pos(label, x - labelW / 2, y - labelH / 2);
    };

    placeLabel(indoorTempLabel, indoorRadius - scalePx(12), 8.0f);
    placeLabel(outdoorTempLabel, outdoorRadius - scalePx(12), 26.0f);
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

void EnvironmentInfoScreen::updateOutdoorSegmentsFromValue(int value) {
    const float normalized = constrain(static_cast<float>(value) / static_cast<float>(ARC_RANGE_MAX), 0.0f, 1.0f);

    for (size_t i = 0; i < OUTDOOR_SEGMENT_COUNT; ++i) {
        if (outdoorSegmentArcs[i] == nullptr) {
            continue;
        }

        const float start = outdoorSegmentStops[i];
        const float end = outdoorSegmentStops[i + 1];
        const float range = end - start;
        float segmentProgress = 0.0f;
        if (range > 0.0f) {
            segmentProgress = constrain((normalized - start) / range, 0.0f, 1.0f);
        }

        const int16_t startAngle = outdoorSegmentStartAngles[i];
        const int16_t endAngle = outdoorSegmentEndAngles[i];
        int16_t span = static_cast<int16_t>(endAngle - startAngle);
        if (span < 0) {
            span = static_cast<int16_t>(span + 360);
        }

        const float currentAngleFloat = static_cast<float>(startAngle) + (segmentProgress * static_cast<float>(span));
        const int16_t currentAngle = normalizeAngle(currentAngleFloat);
        lv_arc_set_angles(outdoorSegmentArcs[i], startAngle, currentAngle);

        const bool segmentVisible = segmentProgress > 0.0f;
        lv_obj_set_style_arc_opa(outdoorSegmentArcs[i],
                                 segmentVisible ? LV_OPA_COVER : LV_OPA_TRANSP,
                                 LV_PART_INDICATOR);
        lv_obj_set_style_shadow_opa(outdoorSegmentArcs[i],
                                    segmentVisible ? LV_OPA_40 : LV_OPA_TRANSP,
                                    LV_PART_INDICATOR);
    }
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
    return std::max(1, (referencePx * gfx->width()) / 240);
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

void EnvironmentInfoScreen::resetScreen() {
    ESP_LOGI(TAG, "resetScreen called - forcing redraw");
    screenInitialized = false;
    dateTimeChanged = true;
    indoorTempChanged = true;
    outdoorTempChanged = true;
    activate();
    lv_anim_del(this, indoorArcAnimExec);
    lv_anim_del(this, outdoorArcAnimExec);
    if (root != nullptr) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
    LvglDisplay::invalidateScreen();
}

