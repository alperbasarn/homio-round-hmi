#include "ModeController.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

namespace {

constexpr const char* TAG = "ModeController";

}  // namespace

ModeController::ModeController(DisplayPanelBackend* displayBackend, TouchPanel* touch)
    : display(displayBackend),
      touchPanel(touch),
      currentMode(MODE_SOUND),
      lastMode(static_cast<Mode>(-1)),
      active(false),
      modeSelected(false),
      pageBackRequested(false),
      ignoreNextRelease(false),
      activatedAtMs(0),
      tapReleaseCount(0),
      firstTapReleaseAtMs(0),
      root(nullptr),
      iconLabel(nullptr),
      titleLabel(nullptr),
      hintLabel(nullptr) {
}

void ModeController::ensureUi() {
    if (root != nullptr || display == nullptr || !LvglDisplay::isInitialized()) {
        return;
    }

    if (!LvglDisplay::lock(-1)) {
        ESP_LOGW(TAG, "LVGL lock unavailable while creating mode controller UI");
        return;
    }

    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, display->width(), display->height());
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);

    iconLabel = lv_label_create(root);
    lv_obj_set_style_text_color(iconLabel, lv_color_hex(0xFFDD55), 0);
    lv_obj_align(iconLabel, LV_ALIGN_CENTER, 0, -scalePx(34));

    titleLabel = lv_label_create(root);
    lv_obj_set_style_text_color(titleLabel, lv_color_white(), 0);
    lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, scalePx(28));

    hintLabel = lv_label_create(root);
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x6B778A), 0);
    lv_label_set_text(hintLabel, "Swipe left/right  |  Double tap select");
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -scalePx(20));

    LvglDisplay::unlock();
}

void ModeController::updateScreen() {
    if (!active) {
        return;
    }

    ensureUi();

    if (currentMode != lastMode) {
        lastMode = currentMode;
        drawModeLogo();
    }

    if (touchPanel && touchPanel->getHasNewGesture()) {
        const touch_gesture_t touchGesture = touchPanel->getLastGesture();

        if (touchGesture == GESTURE_SWIPE_LEFT) {
            previousMode();
            tapReleaseCount = 0;
            ESP_LOGI(TAG, "Swipe left detected - previous mode: %s", modeToString(currentMode));
        } else if (touchGesture == GESTURE_SWIPE_RIGHT) {
            nextMode();
            tapReleaseCount = 0;
            ESP_LOGI(TAG, "Swipe right detected - next mode: %s", modeToString(currentMode));
        } else if (touchGesture == GESTURE_HOLD_RELEASE) {
            const int64_t holdDurationMs = touchPanel->getLastPressDurationMs();
            if (holdDurationMs >= SHORT_HOLD_RELEASE_MS) {
                tapReleaseCount = 0;
                pageBackRequested = true;
                ESP_LOGI(TAG, "Short hold release detected (%lld ms) - requesting page back", holdDurationMs);
            }
        }
    }

    if (touchPanel && touchPanel->getHasNewRelease()) {
        const int64_t elapsedSinceActivation = millis() - activatedAtMs;
        if (ignoreNextRelease || elapsedSinceActivation < STALE_RELEASE_GUARD_MS) {
            ignoreNextRelease = false;
            tapReleaseCount = 0;
        } else {
            const int64_t now = millis();
            if (tapReleaseCount == 0 || (now - firstTapReleaseAtMs) > DOUBLE_TAP_WINDOW_MS) {
                tapReleaseCount = 1;
                firstTapReleaseAtMs = now;
            } else if (++tapReleaseCount >= 2) {
                modeSelected = true;
                tapReleaseCount = 0;
                ESP_LOGI(TAG, "Double tap confirmed - mode selected: %s", modeToString(currentMode));
            }
        }
    } else if (ignoreNextRelease && touchPanel && !touchPanel->isPressed() &&
               (millis() - activatedAtMs) >= STALE_RELEASE_GUARD_MS) {
        ignoreNextRelease = false;
    }

    if (tapReleaseCount > 0 && (millis() - firstTapReleaseAtMs) > DOUBLE_TAP_WINDOW_MS) {
        tapReleaseCount = 0;
    }

    LvglDisplay::taskHandler();
}

void ModeController::nextMode() {
    currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
    drawModeLogo();
}

void ModeController::previousMode() {
    currentMode = static_cast<Mode>((currentMode - 1 + MODE_COUNT) % MODE_COUNT);
    drawModeLogo();
}

void ModeController::setActive(bool isActive) {
    active = isActive;
    ensureUi();

    if (active) {
        modeSelected = false;
        pageBackRequested = false;
        activatedAtMs = millis();
        tapReleaseCount = 0;
        firstTapReleaseAtMs = 0;
        ignoreNextRelease = (touchPanel != nullptr) &&
                            (touchPanel->isPressed() ||
                             touchPanel->getHasNewRelease() ||
                             touchPanel->getHasNewHoldRelease());
        drawModeLogo();
    } else {
        ignoreNextRelease = false;
        tapReleaseCount = 0;
        firstTapReleaseAtMs = 0;
        if (root != nullptr && LvglDisplay::lock(-1)) {
            lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
            LvglDisplay::unlock();
        }
    }
}

void ModeController::setMode(Mode mode) {
    if (mode >= 0 && mode < MODE_COUNT) {
        currentMode = mode;
        if (active) {
            drawModeLogo();
        }
    }
}

void ModeController::drawModeLogo() {
    ensureUi();
    if (root == nullptr || !LvglDisplay::lock(-1)) {
        return;
    }

    lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(root);
    lv_label_set_text(iconLabel, modeIcon(currentMode));
    lv_label_set_text(titleLabel, modeToString(currentMode));
    LvglDisplay::invalidateScreen();
    LvglDisplay::unlock();
}

int64_t ModeController::millis() const {
    return esp_timer_get_time() / 1000;
}

int ModeController::scalePx(int referencePx) const {
    return std::max(1, (referencePx * display->width()) / 240);
}

const char* ModeController::modeToString(Mode mode) const {
    switch (mode) {
        case MODE_LIGHT:
            return "LIGHT";
        case MODE_TEMPERATURE:
            return "TEMPERATURE";
        case MODE_SOUND:
            return "SOUND";
        default:
            return "UNKNOWN";
    }
}

const char* ModeController::modeIcon(Mode mode) const {
    switch (mode) {
        case MODE_LIGHT:
            return "LIGHT";
        case MODE_TEMPERATURE:
            return "TEMP";
        case MODE_SOUND:
            return "SOUND";
        default:
            return "MODE";
    }
}
