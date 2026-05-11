#include "SafeModeScreen.h"
#include "esp_log.h"
#include <algorithm>

static const char* TAG = "SafeModeScreen";

// ---- construction ----------------------------------------------------------

SafeModeScreen::SafeModeScreen(const char* modeLabel)
    : modeLabel_(modeLabel), statusText_("") {}

// ---- public API ------------------------------------------------------------

void SafeModeScreen::show()
{
    if (!LvglDisplay::isInitialized() && !LvglDisplay::init()) {
        ESP_LOGE(TAG, "LVGL init failed — cannot show SafeModeScreen");
        return;
    }
    buildUi();
    lv_scr_load(root_);
    LvglDisplay::invalidateScreen();
    ESP_LOGW(TAG, "Safe-mode screen displayed: %s", modeLabel_);
}

void SafeModeScreen::setStatusText(const std::string& text)
{
    statusText_ = text;
    if (statusLabel_ != nullptr) {
        lv_label_set_text(statusLabel_, text.c_str());
    }
}

// ---- private ---------------------------------------------------------------

int SafeModeScreen::scalePx(int referencePx) const
{
    return std::max(1, (referencePx * LvglDisplay::getWidth()) / 240);
}

void SafeModeScreen::buildUi()
{
    if (root_ != nullptr) return;  // already built

    const int w = LvglDisplay::getWidth();
    const int h = LvglDisplay::getHeight();
    const int cx = w / 2;
    const int cy = h / 2;

    // Dimensions scaled from a 240-px reference
    const int faceR      = scalePx(80);    // outer circle radius
    const int eyeR       = scalePx(8);     // eye dot radius
    const int eyeOffsetX = scalePx(22);    // eye horizontal offset from center
    const int eyeOffsetY = scalePx(20);    // eye vertical offset (above center)
    const int mouthSize  = scalePx(50);    // mouth arc widget size
    const int mouthOffY  = scalePx(20);    // mouth arc centre vertical offset (below center)
    const int mouthWidth = scalePx(8);     // arc line thickness

    // ---------- root (full black screen) ----------
    root_ = lv_obj_create(lv_scr_act());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // ---------- face outline circle ----------
    faceCircle_ = lv_obj_create(root_);
    lv_obj_remove_style_all(faceCircle_);
    lv_obj_set_size(faceCircle_, faceR * 2, faceR * 2);
    lv_obj_set_pos(faceCircle_, cx - faceR, cy - faceR);
    lv_obj_set_style_radius(faceCircle_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(faceCircle_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(faceCircle_, lv_color_hex(0xFF4858), 0);
    lv_obj_set_style_border_width(faceCircle_, scalePx(4), 0);
    lv_obj_clear_flag(faceCircle_, LV_OBJ_FLAG_SCROLLABLE);

    // ---------- left eye ----------
    lv_obj_t* leftEye = lv_obj_create(root_);
    lv_obj_remove_style_all(leftEye);
    lv_obj_set_size(leftEye, eyeR * 2, eyeR * 2);
    lv_obj_set_pos(leftEye, cx - eyeOffsetX - eyeR, cy - eyeOffsetY - eyeR);
    lv_obj_set_style_radius(leftEye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(leftEye, lv_color_hex(0xFF4858), 0);
    lv_obj_set_style_bg_opa(leftEye, LV_OPA_COVER, 0);

    // ---------- right eye ----------
    lv_obj_t* rightEye = lv_obj_create(root_);
    lv_obj_remove_style_all(rightEye);
    lv_obj_set_size(rightEye, eyeR * 2, eyeR * 2);
    lv_obj_set_pos(rightEye, cx + eyeOffsetX - eyeR, cy - eyeOffsetY - eyeR);
    lv_obj_set_style_radius(rightEye, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(rightEye, lv_color_hex(0xFF4858), 0);
    lv_obj_set_style_bg_opa(rightEye, LV_OPA_COVER, 0);

    // ---------- sad mouth (arc, opening upward = frown) ----------
    // LVGL arc bg_angles: 180°–360° is the lower half; we rotate 180° and use
    // the indicator as the frown.  A simpler approach: set bg_angles to cover
    // the bottom half and draw only the indicator there.
    mouthArc_ = lv_arc_create(root_);
    lv_obj_set_size(mouthArc_, mouthSize, mouthSize);
    lv_obj_set_pos(mouthArc_, cx - mouthSize / 2, cy + mouthOffY);
    lv_obj_clear_flag(mouthArc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(mouthArc_, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(mouthArc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_opa(mouthArc_, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_arc_color(mouthArc_, lv_color_hex(0xFF4858), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(mouthArc_, mouthWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(mouthArc_, true, LV_PART_INDICATOR);
    // Rotation 180° + arc from 30°–150° draws a frown at the bottom of the face
    lv_arc_set_rotation(mouthArc_, 180);
    lv_arc_set_bg_angles(mouthArc_, 0, 360);
    lv_arc_set_angles(mouthArc_, 30, 150);

    // ---------- mode name label (below face) ----------
    modeLabel_w = lv_label_create(root_);
    lv_obj_set_style_text_color(modeLabel_w, lv_color_hex(0xFF4858), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_14)
    lv_obj_set_style_text_font(modeLabel_w, &lv_font_montserrat_14, 0);
#endif
    lv_label_set_long_mode(modeLabel_w, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(modeLabel_w, w - scalePx(20));
    lv_obj_set_style_text_align(modeLabel_w, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(modeLabel_w, modeLabel_);
    lv_obj_align(modeLabel_w, LV_ALIGN_BOTTOM_MID, 0, -scalePx(32));

    // ---------- status text label (bottom edge) ----------
    statusLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(0x888888), 0);
#if defined(CONFIG_LV_FONT_MONTSERRAT_12)
    lv_obj_set_style_text_font(statusLabel_, &lv_font_montserrat_12, 0);
#endif
    lv_label_set_long_mode(statusLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(statusLabel_, w - scalePx(20));
    lv_obj_set_style_text_align(statusLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(statusLabel_, statusText_.c_str());
    lv_obj_align(statusLabel_, LV_ALIGN_BOTTOM_MID, 0, -scalePx(10));
}
