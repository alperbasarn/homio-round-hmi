#include "ModeController.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <algorithm>

static const char* TAG = "ModeController";

// Colours per mode (RGB888)
static constexpr uint32_t COLOR_SOUND       = 0x00BFFF;  // deep sky blue
static constexpr uint32_t COLOR_LIGHT       = 0xFFD700;  // gold
static constexpr uint32_t COLOR_TEMPERATURE = 0xFF4500;  // orange-red

ModeController::ModeController()
    :       currentMode(MODE_SOUND), lastMode(static_cast<Mode>(-1)), active(false),
      modeSelected(false), pageBackRequested(false),
      root(nullptr), iconObj(nullptr), nameLabel(nullptr), hintLabel(nullptr),
      lvglReady(false) {
}

int64_t ModeController::millis() const {
    return esp_timer_get_time() / 1000;
}

const char* ModeController::modeToString(Mode mode) const {
    switch (mode) {
        case MODE_LIGHT:       return "LIGHT";
        case MODE_TEMPERATURE: return "TEMPERATURE";
        case MODE_SOUND:       return "SOUND";
        default:               return "UNKNOWN";
    }
}

void ModeController::ensureUi() {
    if (lvglReady) return;
    if (!LvglDisplay::isInitialized()) return;
    lvglReady = true;
    buildUi();
}

void ModeController::buildUi() {
    if (root != nullptr) return;

    const int w = LvglDisplay::getWidth();
    const int h = LvglDisplay::getHeight();
    const int iconDiam = std::max(60, w / 4);

    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, w, h);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);  // hidden until activated

    // Mode icon — a filled circle, colour set per mode in refreshUi
    iconObj = lv_obj_create(root);
    lv_obj_remove_style_all(iconObj);
    lv_obj_set_size(iconObj, iconDiam, iconDiam);
    lv_obj_set_style_radius(iconObj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(iconObj, LV_OPA_COVER, 0);
    lv_obj_align(iconObj, LV_ALIGN_CENTER, 0, -iconDiam / 3);

#if defined(CONFIG_LV_FONT_MONTSERRAT_32)
    const lv_font_t* font = (w >= 400) ? &lv_font_montserrat_32 : LV_FONT_DEFAULT;
#else
    const lv_font_t* font = LV_FONT_DEFAULT;
#endif

    // Mode name label
    nameLabel = lv_label_create(root);
    lv_obj_set_style_text_font(nameLabel, font, 0);
    lv_obj_set_style_text_color(nameLabel, lv_color_white(), 0);
    lv_label_set_text(nameLabel, "SOUND");
    lv_obj_align(nameLabel, LV_ALIGN_CENTER, 0, iconDiam / 3 + 8);

    // Swipe hint
    hintLabel = lv_label_create(root);
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x606060), 0);
    lv_label_set_text(hintLabel, "\xe2\x86\x90 swipe \xe2\x86\x92  \xe2\x80\xa2  double-tap selects  \xe2\x80\xa2  hold 3 s: back");
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -16);

    LvglDisplay::invalidateScreen();
}

void ModeController::refreshUi() {
    if (!lvglReady || root == nullptr) return;

    uint32_t color;
    const char* name;
    switch (currentMode) {
        case MODE_LIGHT:       color = COLOR_LIGHT;       name = "LIGHT";       break;
        case MODE_TEMPERATURE: color = COLOR_TEMPERATURE; name = "TEMPERATURE"; break;
        default:               color = COLOR_SOUND;       name = "SOUND";       break;
    }

    lv_obj_set_style_bg_color(iconObj, lv_color_hex(color), 0);
    lv_label_set_text(nameLabel, name);
    lv_obj_set_style_text_color(nameLabel, lv_color_hex(color), 0);
    lv_obj_align(nameLabel, LV_ALIGN_CENTER, 0, lv_obj_get_height(iconObj) / 3 + 8);
    LvglDisplay::invalidateScreen();
}

void ModeController::updateScreen() {
    if (!active) return;

    ensureUi();

    if (currentMode != lastMode) {
        lastMode = currentMode;
        refreshUi();
        ESP_LOGI(TAG, "Mode: %s", modeToString(currentMode));
    }

    LvglDisplay::taskHandler();

}

void ModeController::nextMode() {
    currentMode = static_cast<Mode>((currentMode + 1) % MODE_COUNT);
    refreshUi();
}

void ModeController::previousMode() {
    currentMode = static_cast<Mode>((currentMode - 1 + MODE_COUNT) % MODE_COUNT);
    refreshUi();
}

void ModeController::setActive(bool isActive) {
    active = isActive;
    if (active) {
        modeSelected = false;
        pageBackRequested = false;
        ensureUi();
        if (root) {
            lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
            refreshUi();
        }
    }
}

void ModeController::setMode(Mode mode) {
    if (mode >= 0 && mode < MODE_COUNT) {
        currentMode = mode;
        if (active) refreshUi();
    }
}

void ModeController::deactivate() {
    active = false;
    if (lvglReady && root != nullptr) {
        lv_obj_add_flag(root, LV_OBJ_FLAG_HIDDEN);
    }
}

void ModeController::onInput(const input::InputEvent& event) {
    if (!active) {
        return;
    }
    switch (event.type) {
        case input::InputType::Gesture:
            if (event.gesture == input::Gesture::SwipeLeft) {
                previousMode();
            } else if (event.gesture == input::Gesture::SwipeRight) {
                nextMode();
            } else if (event.gesture == input::Gesture::DoubleTap) {
                modeSelected = true;
            }
            break;
        case input::InputType::TouchHoldRelease:
            pageBackRequested = true;  // the router only emits this at 3 s or more (D13)
            break;
        default:
            break;
    }
}
