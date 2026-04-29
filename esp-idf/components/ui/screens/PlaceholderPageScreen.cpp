#include "PlaceholderPageScreen.h"
#include "esp_log.h"
#include <algorithm>
#include <utility>

namespace {

constexpr const char* TAG = "PlaceholderPage";

}  // namespace

PlaceholderPageScreen::PlaceholderPageScreen(TouchPanel* touch)
    : touchPanel(touch),
      lvglReady(false),
      screenInitialized(false),
      title("Placeholder"),
      subtitle("Not implemented yet"),
      root(nullptr),
      titleLabel(nullptr),
      subtitleLabel(nullptr),
      hintLabel(nullptr),
      playPauseButton(nullptr),
      playPauseIcon(nullptr),
      pcButtonPressed(false),
      pcButtonWasPressed(false) {
}

void PlaceholderPageScreen::setContent(const std::string& pageTitle, const std::string& pageSubtitle) {
    title = pageTitle;
    subtitle = pageSubtitle;
    if (root != nullptr) {
        refreshUi(false);
    }
}

void PlaceholderPageScreen::setPlayPauseAction(std::function<void()> action) {
    playPauseAction = std::move(action);
}

void PlaceholderPageScreen::ensureUi() {
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

void PlaceholderPageScreen::buildUi() {
    if (root != nullptr) {
        return;
    }

    const int displayW = LvglDisplay::getWidth();
    const int displayH = LvglDisplay::getHeight();

    root = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, displayW, displayH);
    lv_obj_set_pos(root, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);

    titleLabel = lv_label_create(root);
    lv_obj_set_style_text_color(titleLabel, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_align(titleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(titleLabel, displayW - 2 * scalePx(16));
    lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -scalePx(40));

    subtitleLabel = lv_label_create(root);
    lv_obj_set_style_text_color(subtitleLabel, lv_color_hex(0x9AA8BA), 0);
    lv_obj_set_style_text_align(subtitleLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(subtitleLabel, displayW - 2 * scalePx(24));
    lv_obj_align(subtitleLabel, LV_ALIGN_CENTER, 0, scalePx(4));

    hintLabel = lv_label_create(root);
    lv_obj_set_style_text_color(hintLabel, lv_color_hex(0x5A6678), 0);
    lv_obj_set_style_text_align(hintLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(hintLabel, "Swipe up or down to move");
    lv_obj_align(hintLabel, LV_ALIGN_BOTTOM_MID, 0, -scalePx(18));

    playPauseButton = lv_obj_create(root);
    lv_obj_remove_style_all(playPauseButton);
    const int buttonSize = scalePx(96);
    lv_obj_set_size(playPauseButton, buttonSize, buttonSize);
    lv_obj_set_style_radius(playPauseButton, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(playPauseButton, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(playPauseButton, scalePx(2), 0);
    lv_obj_clear_flag(playPauseButton, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(playPauseButton, LV_ALIGN_CENTER, 0, scalePx(14));

    playPauseIcon = lv_label_create(playPauseButton);
    lv_obj_set_style_text_color(playPauseIcon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(playPauseIcon, &lv_font_montserrat_48, 0);
    lv_label_set_text(playPauseIcon, LV_SYMBOL_PLAY);
    lv_obj_center(playPauseIcon);

    refreshUi(true);
}

void PlaceholderPageScreen::refreshUi(bool forceFullRefresh) {
    if (root == nullptr) {
        return;
    }

    if (forceFullRefresh) {
        lv_obj_clear_flag(root, LV_OBJ_FLAG_HIDDEN);
        screenInitialized = true;
    }

    const bool pcControl = isPcControlPage();

    lv_label_set_text(titleLabel, title.c_str());
    lv_label_set_text(subtitleLabel, subtitle.c_str());
    if (pcControl) {
        lv_obj_align(titleLabel, LV_ALIGN_TOP_MID, 0, scalePx(32));
        lv_obj_add_flag(subtitleLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(playPauseButton, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_align(titleLabel, LV_ALIGN_CENTER, 0, -scalePx(40));
        lv_obj_clear_flag(subtitleLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(hintLabel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(playPauseButton, LV_OBJ_FLAG_HIDDEN);
    }
    updatePlayPauseVisual();
    LvglDisplay::invalidateScreen();
}

void PlaceholderPageScreen::destroyUi() {
    if (root != nullptr) {
        lv_obj_del(root);
        root = nullptr;
        titleLabel = nullptr;
        subtitleLabel = nullptr;
        hintLabel = nullptr;
        playPauseButton = nullptr;
        playPauseIcon = nullptr;
    }
    pcButtonPressed = false;
    pcButtonWasPressed = false;
    screenInitialized = false;
}

void PlaceholderPageScreen::activate() {
    screenInitialized = false;
}

void PlaceholderPageScreen::deactivate() {
    destroyUi();
}

void PlaceholderPageScreen::resetScreen() {
    screenInitialized = false;
    destroyUi();
    LvglDisplay::invalidateScreen();
}

void PlaceholderPageScreen::update() {
    ensureUi();
    if (lvglReady) {
        refreshUi(!screenInitialized);
        updatePcControlTouch();
        LvglDisplay::taskHandler();
    }
}

int PlaceholderPageScreen::scalePx(int referencePx) const {
    return std::max(1, (referencePx * LvglDisplay::getWidth()) / 240);
}

bool PlaceholderPageScreen::isPcControlPage() const {
    return title == "PC Control";
}

bool PlaceholderPageScreen::isInsidePlayPauseButton(int x, int y) const {
    if (playPauseButton == nullptr) {
        return false;
    }

    lv_area_t area;
    lv_obj_get_coords(playPauseButton, &area);
    return x >= area.x1 && x <= area.x2 && y >= area.y1 && y <= area.y2;
}

void PlaceholderPageScreen::updatePcControlTouch() {
    if (!isPcControlPage() || touchPanel == nullptr || playPauseButton == nullptr) {
        pcButtonPressed = false;
        pcButtonWasPressed = false;
        return;
    }

    pcButtonWasPressed = pcButtonPressed;
    pcButtonPressed = touchPanel->isPressed() &&
                      isInsidePlayPauseButton(touchPanel->getTouchX(), touchPanel->getTouchY());

    if (touchPanel->getHasNewRelease() && pcButtonWasPressed && playPauseAction) {
        playPauseAction();
    }

    if (pcButtonPressed != pcButtonWasPressed) {
        updatePlayPauseVisual();
        LvglDisplay::invalidateScreen();
    }
}

void PlaceholderPageScreen::updatePlayPauseVisual() {
    if (playPauseButton == nullptr || playPauseIcon == nullptr) {
        return;
    }

    const uint32_t fill = pcButtonPressed ? 0x31415A : 0x111820;
    const uint32_t border = pcButtonPressed ? 0xFFFFFF : 0x546274;
    lv_obj_set_style_bg_color(playPauseButton, lv_color_hex(fill), 0);
    lv_obj_set_style_border_color(playPauseButton, lv_color_hex(border), 0);
}
