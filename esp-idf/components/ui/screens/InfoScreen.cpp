#include "InfoScreen.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>

namespace {

constexpr const char* TAG = "InfoScreen";
constexpr float PI = 3.14159265358979323846f;
const std::array<lv_color_t, 4> OUTDOOR_SEGMENT_COLORS = {
    lv_color_hex(0xFF3A55),
    lv_color_hex(0xFF7B39),
    lv_color_hex(0xFFD85C),
    lv_color_hex(0x39D9FF)
};

}  // namespace

InfoScreen::InfoScreen(LGFX* graphics, TouchPanel* touch)
    : gfx(graphics),
      touchPanel(touch),
      screenInitialized(false),
      pageBackRequested(false),
      lvglReady(false),
      currentDate("1994/03/11"),
      currentTime("04:30"),
      formattedDate("11 MAR"),
      currentDayOfWeek("FRI"),
      lastFormattedDate(""),
      lastFormattedTime(""),
      colonVisible(true),
      lastColonToggleTime(0),
      indoorTemp(18.0f),
      outdoorTemp(0.0f),
      indoorTempChanged(false),
      outdoorTempChanged(false),
      dateTimeChanged(true),
      networkStatusChanged(true),
      wifiConnected(false),
      internetConnected(false),
      mqttConnected(false),
      wifiStrength(0),
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
      networkLabel(nullptr),
      outdoorSegmentStops{0.0f, 0.25f, 0.50f, 0.75f, 1.0f},
      currentIndoorArcValue(0),
      currentOutdoorArcValue(0),
      targetIndoorArcValue(0),
      targetOutdoorArcValue(0) {
    formatDate();
    lastColonToggleTime = millis();
}

void InfoScreen::update() {
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

    // Poll network state and outside temperature periodically.
    updateNetworkStatus();

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

    // Swipe/tap exits to mode controller.
    if (touchPanel && touchPanel->getHasNewGesture()) {
        resetLastActivityTime();
        touch_gesture_t touchGesture = touchPanel->getLastGesture();
        if (touchGesture == GESTURE_SWIPE_RIGHT) {
            pageBackRequested = true;
            screenInitialized = false;
            if (root) {
                lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
            }
            ESP_LOGI(TAG, "Swipe right detected, navigating to mode controller");
        }
    }

    if (touchPanel && touchPanel->getHasNewRelease()) {
        resetLastActivityTime();
        pageBackRequested = true;
        screenInitialized = false;
        if (root) {
            lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
        }
        ESP_LOGI(TAG, "Tap release detected, navigating to mode controller");
    }
}

void InfoScreen::ensureUi() {
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

void InfoScreen::buildUi() {
    if (root != nullptr) {
        return;
    }

    const int displayW = gfx->width();
    const int displayH = gfx->height();
    const int displaySize = std::min(displayW, displayH);

    const int framePadding = std::max(scalePx(10), displaySize / 20);
    const int outerDiameter = displaySize - (2 * framePadding);
    const int outerArcWidth = std::max(scalePx(14), displaySize / 13);
    const int baseInnerArcWidth = std::max(scalePx(11), static_cast<int>(outerArcWidth * 0.75f));
    const int ringGap = std::max(scalePx(2), outerArcWidth / 12);
    const int innerDiameter = std::max(scalePx(120), outerDiameter - 2 * (outerArcWidth + ringGap));
    const int centerGap = std::max(scalePx(1), baseInnerArcWidth / 10);
    const int baseCenterDiameter = std::max(scalePx(90), innerDiameter - 2 * (baseInnerArcWidth + centerGap));
    const int centerDiameter = std::max(scalePx(81), static_cast<int>(std::lround(baseCenterDiameter * 0.90f)));
    const int innerArcWidth = std::max(baseInnerArcWidth, ((innerDiameter - centerDiameter) / 2) - centerGap);

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
        lv_arc_set_rotation(arc, static_cast<int16_t>(std::round(ARC_START_ANGLE)));
        lv_arc_set_bg_angles(arc, 0, static_cast<int16_t>(std::round(ARC_LENGTH)));
        lv_arc_set_range(arc, 0, ARC_RANGE_MAX);
        lv_arc_set_value(arc, 0);
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
                             static_cast<int16_t>(std::round(segStart)),
                             static_cast<int16_t>(std::round(segEnd)));

        outdoorSegmentArcs[i] = segmentArc;
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

    const int labelStackOffset = std::min(scalePx(52), centerDiameter / 3);

    timeLabel = lv_label_create(root);
    lv_obj_set_style_text_color(timeLabel, lv_color_white(), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_48)
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_48, 0);
#elif defined(CONFIG_LV_FONT_MONTSERRAT_32)
    lv_obj_set_style_text_font(timeLabel, &lv_font_montserrat_32, 0);
#endif
    lv_label_set_text(timeLabel, "--:--");
    lv_obj_align(timeLabel, LV_ALIGN_CENTER, 0, 0);

    dayLabel = lv_label_create(root);
    lv_obj_set_style_text_color(dayLabel, lv_color_hex(0x9AA8BA), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_32)
    lv_obj_set_style_text_font(dayLabel, &lv_font_montserrat_32, 0);
#elif defined(CONFIG_LV_FONT_MONTSERRAT_28)
    lv_obj_set_style_text_font(dayLabel, &lv_font_montserrat_28, 0);
#endif
    lv_label_set_text(dayLabel, "---");
    lv_obj_align(dayLabel, LV_ALIGN_CENTER, 0, -labelStackOffset);

    dateLabel = lv_label_create(root);
    lv_obj_set_style_text_color(dateLabel, lv_color_hex(0xC7D0DD), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_32)
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_32, 0);
#elif defined(CONFIG_LV_FONT_MONTSERRAT_28)
    lv_obj_set_style_text_font(dateLabel, &lv_font_montserrat_28, 0);
#endif
    lv_label_set_text(dateLabel, "-- ---");
    lv_obj_align(dateLabel, LV_ALIGN_CENTER, 0, labelStackOffset);

    indoorTempLabel = lv_label_create(root);
    lv_obj_set_style_text_color(indoorTempLabel, lv_color_hex(0xA0FF8C), 0);
    lv_label_set_text(indoorTempLabel, "18C");

    outdoorTempLabel = lv_label_create(root);
    lv_obj_set_style_text_color(outdoorTempLabel, lv_color_hex(0x6AC7FF), 0);
    lv_label_set_text(outdoorTempLabel, "0C");

    networkLabel = lv_label_create(root);
    lv_obj_set_style_text_color(networkLabel, lv_color_hex(0x8A97A8), 0);
    lv_label_set_text(networkLabel, "WiFi --  NET --  MQTT --");
    lv_obj_align(networkLabel, LV_ALIGN_BOTTOM_MID, 0, -scalePx(10));

    positionTemperatureLabels();
    LvglDisplay::invalidateScreen();
}

void InfoScreen::updateUi(bool forceFullRefresh) {
    if (!lvglReady || root == nullptr) {
        return;
    }

    if (forceFullRefresh) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        screenInitialized = true;
        dateTimeChanged = true;
        indoorTempChanged = true;
        outdoorTempChanged = true;
        networkStatusChanged = true;
        currentIndoorArcValue = 0;
        currentOutdoorArcValue = 0;
        lv_anim_del(indoorArc, indoorArcAnimExec);
        lv_anim_del(this, outdoorArcAnimExec);
        if (indoorArc) {
            lv_arc_set_value(indoorArc, 0);
        }
        updateOutdoorSegmentsFromValue(0);
        LvglDisplay::invalidateScreen();
    }

    const float indoorNormalized = constrain((indoorTemp - 0.0f) / 40.0f, 0.0f, 1.0f);
    const float outdoorNormalized = constrain((outdoorTemp - (-20.0f)) / 70.0f, 0.0f, 1.0f);
    const int indoorValue = static_cast<int>(std::lround(indoorNormalized * ARC_RANGE_MAX));
    const int outdoorValue = static_cast<int>(std::lround(outdoorNormalized * ARC_RANGE_MAX));

    if (indoorTempChanged || forceFullRefresh) {
        targetIndoorArcValue = indoorValue;
        const lv_color_t indoorColor = getTemperatureColor(indoorTemp, true);
        lv_obj_set_style_arc_color(indoorArc, indoorColor, LV_PART_INDICATOR);
        lv_obj_set_style_shadow_color(indoorArc, indoorColor, LV_PART_INDICATOR);
        startIndoorArcAnimation(targetIndoorArcValue, false);

        char tempBuffer[16];
        std::snprintf(tempBuffer, sizeof(tempBuffer), "IN %dC", static_cast<int>(std::lround(indoorTemp)));
        lv_label_set_text(indoorTempLabel, tempBuffer);
        lv_obj_set_style_text_color(indoorTempLabel, indoorColor, 0);
    }

    if (outdoorTempChanged || forceFullRefresh) {
        targetOutdoorArcValue = outdoorValue;
        startOutdoorArcAnimation(targetOutdoorArcValue, false);

        const lv_color_t outdoorColor = getTemperatureColor(outdoorTemp, false);
        char tempBuffer[16];
        std::snprintf(tempBuffer, sizeof(tempBuffer), "OUT %dC", static_cast<int>(std::lround(outdoorTemp)));
        lv_label_set_text(outdoorTempLabel, tempBuffer);
        lv_obj_set_style_text_color(outdoorTempLabel, outdoorColor, 0);
    }

    if (dateTimeChanged || forceFullRefresh) {
        lv_label_set_text(timeLabel, getTimeStringForDisplay().c_str());
        lv_label_set_text(dayLabel, currentDayOfWeek.empty() ? "---" : currentDayOfWeek.c_str());
        lv_label_set_text(dateLabel, formattedDate.empty() ? "-- ---" : formattedDate.c_str());
    }

    if (networkStatusChanged || forceFullRefresh) {
        char networkText[64];
        const int bars = constrain(wifiStrength, 0, 3);
        std::snprintf(networkText, sizeof(networkText),
                      "WiFi %d/3  NET %s  MQTT %s",
                      bars,
                      internetConnected ? "ON" : "OFF",
                      mqttConnected ? "ON" : "OFF");
        lv_label_set_text(networkLabel, networkText);

        lv_color_t netColor = lv_color_hex(0x8A97A8);
        if (!wifiConnected) {
            netColor = lv_color_hex(0xB15F5F);
        } else if (internetConnected) {
            netColor = lv_color_hex(0x7CD97A);
        } else {
            netColor = lv_color_hex(0xD7C06E);
        }
        lv_obj_set_style_text_color(networkLabel, netColor, 0);
    }

    positionTemperatureLabels();

    dateTimeChanged = false;
    indoorTempChanged = false;
    outdoorTempChanged = false;
    networkStatusChanged = false;
}

void InfoScreen::positionTemperatureLabels() {
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

void InfoScreen::updateOutdoorSegmentsFromValue(int value) {
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

        const int segmentValue = static_cast<int>(std::lround(segmentProgress * ARC_RANGE_MAX));
        lv_arc_set_value(outdoorSegmentArcs[i], segmentValue);
        lv_obj_set_style_arc_opa(outdoorSegmentArcs[i],
                                 segmentValue > 0 ? LV_OPA_COVER : LV_OPA_TRANSP,
                                 LV_PART_INDICATOR);
        lv_obj_set_style_shadow_opa(outdoorSegmentArcs[i],
                                    segmentValue > 0 ? LV_OPA_40 : LV_OPA_TRANSP,
                                    LV_PART_INDICATOR);
    }
}

void InfoScreen::startIndoorArcAnimation(int targetValue, bool immediate) {
    if (indoorArc == nullptr) {
        return;
    }

    lv_anim_del(indoorArc, indoorArcAnimExec);

    if (immediate) {
        lv_arc_set_value(indoorArc, targetValue);
        currentIndoorArcValue = targetValue;
        return;
    }

    const int startValue = lv_arc_get_value(indoorArc);
    if (startValue == targetValue) {
        return;
    }

    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, indoorArc);
    lv_anim_set_values(&anim, startValue, targetValue);
    lv_anim_set_time(&anim, ANIMATION_DURATION);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, indoorArcAnimExec);
    lv_anim_start(&anim);

    currentIndoorArcValue = targetValue;
}

void InfoScreen::startOutdoorArcAnimation(int targetValue, bool immediate) {
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

void InfoScreen::indoorArcAnimExec(void* var, int32_t value) {
    lv_obj_t* arc = static_cast<lv_obj_t*>(var);
    if (arc == nullptr) {
        return;
    }

    lv_arc_set_value(arc, static_cast<int16_t>(value));
}

void InfoScreen::outdoorArcAnimExec(void* var, int32_t value) {
    InfoScreen* self = static_cast<InfoScreen*>(var);
    if (self == nullptr) {
        return;
    }

    self->currentOutdoorArcValue = static_cast<int>(value);
    self->updateOutdoorSegmentsFromValue(self->currentOutdoorArcValue);
}

lv_color_t InfoScreen::getTemperatureColor(float temperature, bool isIndoor) {
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

std::string InfoScreen::getTimeStringForDisplay() const {
    std::string displayTime = currentTime.empty() ? "--:--" : currentTime;
    size_t colonPos = displayTime.find(':');
    if (!colonVisible && colonPos != std::string::npos) {
        displayTime[colonPos] = ' ';
    }
    return displayTime;
}

int InfoScreen::scalePx(int referencePx) const {
    return std::max(1, (referencePx * gfx->width()) / 240);
}

void InfoScreen::updateNetworkStatus() {
    if (!networkStatusCallback) {
        return;
    }

    bool newWifi = false;
    bool newInternet = false;
    bool newMqtt = false;
    int newStrength = 0;
    networkStatusCallback(newWifi, newInternet, newMqtt, newStrength);

    if (newWifi != wifiConnected || newInternet != internetConnected ||
        newMqtt != mqttConnected || newStrength != wifiStrength) {
        wifiConnected = newWifi;
        internetConnected = newInternet;
        mqttConnected = newMqtt;
        wifiStrength = newStrength;
        networkStatusChanged = true;
    }
}

void InfoScreen::formatDate() {
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

std::string InfoScreen::getMonthName(int month) {
    static constexpr const char* MONTHS[] = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };

    if (month >= 1 && month <= 12) {
        return MONTHS[month - 1];
    }
    return "---";
}

void InfoScreen::updateDateTime(const std::string& date, const std::string& time, const std::string& dayOfWeek) {
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

void InfoScreen::updateIndoorTemperature(float temperature) {
    if (std::abs(temperature - indoorTemp) > 0.5f) {
        indoorTemp = temperature;
        indoorTempChanged = true;
    }
}

void InfoScreen::updateOutdoorTemperature(float temperature) {
    if (std::abs(temperature - outdoorTemp) > 0.5f) {
        outdoorTemp = temperature;
        outdoorTempChanged = true;
    }
}

void InfoScreen::resetLastActivityTime() {
    lastActivityTime = millis();
    inactivityTimeoutReached = false;
}

bool InfoScreen::isInactivityTimeoutReached() {
    if ((millis() - lastActivityTime) > INACTIVITY_TIMEOUT) {
        inactivityTimeoutReached = true;
    }
    return inactivityTimeoutReached;
}

bool InfoScreen::isPageBackRequested() {
    return pageBackRequested;
}

void InfoScreen::resetPageBackRequest() {
    pageBackRequested = false;
}

void InfoScreen::resetScreen() {
    ESP_LOGI(TAG, "resetScreen called - forcing redraw");
    screenInitialized = false;
    dateTimeChanged = true;
    indoorTempChanged = true;
    outdoorTempChanged = true;
    networkStatusChanged = true;
    if (root != nullptr) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
    LvglDisplay::invalidateScreen();
}
