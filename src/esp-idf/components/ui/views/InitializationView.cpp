#include "InitializationView.h"

#include <algorithm>
#include <cmath>

#include "LvglDisplay.h"
#include "sdkconfig.h"

namespace ui {

int16_t InitializationView::normalizeAngle(float degrees) {
    float n = std::fmod(degrees, 360.0f);
    if (n < 0.0f) {
        n += 360.0f;
    }
    return static_cast<int16_t>(std::lround(n));
}

void InitializationView::doBuild(lv_obj_t* parent) {
    const int w = panelWidth();
    const int h = panelHeight();
    const int padding = std::max(2, w / 32);
    const int arcDiam = std::min(w, h) - 2 * padding;
    const int arcWidth = std::max(12, (14 * w) / 240);
    const int discDiam = static_cast<int>(arcDiam * 0.6f);

    // Full-screen black root
    root_ = lv_obj_create(parent != nullptr ? parent : lv_scr_act());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    // Progress arc: dark-blue track, red indicator
    arc_ = lv_arc_create(root_);
    lv_obj_set_size(arc_, arcDiam, arcDiam);
    lv_obj_center(arc_);
    lv_obj_clear_flag(arc_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(arc_, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_obj_set_style_arc_width(arc_, arcWidth, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_, arcWidth, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc_, true, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc_, lv_color_hex(0x121A2A), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_, lv_color_hex(0xFF4858), LV_PART_INDICATOR);
    lv_arc_set_mode(arc_, LV_ARC_MODE_NORMAL);
    lv_arc_set_rotation(arc_, 0);
    const int16_t bgStart = normalizeAngle(kArcStartDeg);
    const int16_t bgEnd = normalizeAngle(kArcStartDeg + kArcSpanDeg);
    lv_arc_set_bg_angles(arc_, bgStart, bgEnd);
    lv_arc_set_angles(arc_, bgStart, bgStart);  // empty indicator

    // Black centre disc masks the arc interior
    disc_ = lv_obj_create(root_);
    lv_obj_remove_style_all(disc_);
    lv_obj_set_size(disc_, discDiam, discDiam);
    lv_obj_center(disc_);
    lv_obj_set_style_radius(disc_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(disc_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(disc_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(disc_, LV_OBJ_FLAG_SCROLLABLE);

    // Montserrat 32 on the AMOLED panels (>= 400 px), the default face below that
#if defined(CONFIG_LV_FONT_MONTSERRAT_32)
    const lv_font_t* font = (w >= 400) ? &lv_font_montserrat_32 : LV_FONT_DEFAULT;
#else
    const lv_font_t* font = LV_FONT_DEFAULT;
#endif

    qLabel_ = lv_label_create(disc_);
    lv_obj_set_style_text_font(qLabel_, font, 0);
    lv_obj_set_style_text_color(qLabel_, lv_color_hex(0xFF0000), 0);
    lv_label_set_text(qLabel_, "Q");

    nobLabel_ = lv_label_create(disc_);
    lv_obj_set_style_text_font(nobLabel_, font, 0);
    lv_obj_set_style_text_color(nobLabel_, lv_color_white(), 0);
    lv_label_set_text(nobLabel_, "NOB");

    // The two labels side by side in the disc centre
    lv_obj_update_layout(disc_);
    const int qW = lv_obj_get_width(qLabel_);
    const int nobW = lv_obj_get_width(nobLabel_);
    const int textH = lv_obj_get_height(qLabel_);
    const int totalW = qW + nobW;
    const int discW = lv_obj_get_width(disc_);
    const int discH = lv_obj_get_height(disc_);
    lv_obj_set_pos(qLabel_, (discW - totalW) / 2, (discH - textH) / 2);
    lv_obj_set_pos(nobLabel_, (discW - totalW) / 2 + qW, (discH - textH) / 2);

    currentPct_ = 0;
    targetPct_ = 0;
    LvglDisplay::invalidateScreen();
}

void InitializationView::doDestroy() {
    lv_anim_del(this, arcAnimExec);
    if (root_ != nullptr) {
        lv_obj_del(root_);
    }
    root_ = nullptr;
    arc_ = nullptr;
    disc_ = nullptr;
    qLabel_ = nullptr;
    nobLabel_ = nullptr;
    currentPct_ = 0;
    targetPct_ = 0;
}

void InitializationView::doSetVisible(bool visible) {
    if (root_ == nullptr) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
    }
}

void InitializationView::applyArcValue(int percent) {
    if (arc_ == nullptr) {
        return;
    }
    const int clamped = std::clamp(percent, 0, 100);
    const float endAngle = kArcStartDeg + kArcSpanDeg * (static_cast<float>(clamped) / 100.0f);
    lv_arc_set_angles(arc_, normalizeAngle(kArcStartDeg), normalizeAngle(endAngle));
}

void InitializationView::arcAnimExec(void* var, int32_t value) {
    auto* self = static_cast<InitializationView*>(var);
    if (self == nullptr) {
        return;
    }
    self->currentPct_ = static_cast<int>(value);
    self->applyArcValue(self->currentPct_);
}

void InitializationView::doRender(const model::NavigationState& state) {
    const int target = std::clamp(static_cast<int>(state.initProgressPct), 0, 100);
    if (target == targetPct_) {
        return;
    }
    targetPct_ = target;
    lv_anim_del(this, arcAnimExec);
    if (currentPct_ == targetPct_) {
        return;
    }
    lv_anim_t anim;
    lv_anim_init(&anim);
    lv_anim_set_var(&anim, this);
    lv_anim_set_values(&anim, currentPct_, targetPct_);
    lv_anim_set_time(&anim, kArcAnimationMs);
    lv_anim_set_path_cb(&anim, lv_anim_path_ease_out);
    lv_anim_set_exec_cb(&anim, arcAnimExec);
    lv_anim_start(&anim);
}

}  // namespace ui
