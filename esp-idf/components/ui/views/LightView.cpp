#include "LightView.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "LvglDisplay.h"
#include "sdkconfig.h"

namespace ui {

namespace {
constexpr uint32_t kGrey = 0x3A4150u;
constexpr uint32_t kDimGrey = 0x22262Eu;
constexpr uint32_t kOnline = 0x34C759u;
constexpr uint32_t kOffline = 0x6B778Au;
constexpr uint32_t kText = 0xDDE7F5u;
constexpr uint32_t kHint = 0x6B778Au;
}  // namespace

void LightView::doBuild(lv_obj_t* parent) {
    const int w = panelWidth();
    const int h = panelHeight();
    root_ = lv_obj_create(parent != nullptr ? parent : lv_scr_act());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    fill_ = lv_obj_create(root_);
    lv_obj_remove_style_all(fill_);
    lv_obj_set_width(fill_, w);
    lv_obj_set_style_bg_opa(fill_, LV_OPA_COVER, 0);

    // The rim chooser: eight arcs, hidden until a strip occupies them. They cover exactly the rim
    // annulus the router classifies as HitZone::Rim (140..190 px of a 466 px side), so a tap or a
    // hold on a visible segment is a rim event to the controller.
    const int side = std::min(w, h);
    const int arcDiam = (2 * 190 * side) / 466;
    const int arcWidth = std::max(8, (50 * side) / 466);
    for (size_t i = 0; i < model::LedRegistryState::kSlots; ++i) {
        lv_obj_t* arc = lv_arc_create(root_);
        lv_obj_set_size(arc, arcDiam, arcDiam);
        lv_obj_center(arc);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
        lv_obj_set_style_arc_opa(arc, LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_arc_width(arc, arcWidth, LV_PART_INDICATOR);
        lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
        lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
        lv_arc_set_rotation(arc, 270);  // 0 deg at 12 o'clock, like the controller
        lv_arc_set_bg_angles(arc, 0, 360);
        lv_arc_set_angles(arc, 0, 0);
        lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
        rimArcs_[i] = arc;
    }

#if defined(CONFIG_LV_FONT_MONTSERRAT_48)
    const lv_font_t* bigFont = (w >= 400) ? &lv_font_montserrat_48 : LV_FONT_DEFAULT;
#else
    const lv_font_t* bigFont = LV_FONT_DEFAULT;
#endif
#if defined(CONFIG_LV_FONT_MONTSERRAT_28)
    const lv_font_t* textFont = (w >= 400) ? &lv_font_montserrat_28 : LV_FONT_DEFAULT;
#else
    const lv_font_t* textFont = LV_FONT_DEFAULT;
#endif
    levelLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_font(levelLabel_, bigFont, 0);
    lv_obj_set_style_text_color(levelLabel_, lv_color_white(), 0);
    lv_label_set_text(levelLabel_, "--%");
    lv_obj_align(levelLabel_, LV_ALIGN_CENTER, 0, -scalePx(10));

    effectLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_font(effectLabel_, textFont, 0);
    lv_obj_set_style_text_color(effectLabel_, lv_color_hex(kText), 0);
    lv_label_set_text(effectLabel_, "");
    lv_obj_align(effectLabel_, LV_ALIGN_CENTER, 0, scalePx(24));

    nameLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_font(nameLabel_, textFont, 0);
    lv_obj_set_style_text_color(nameLabel_, lv_color_hex(kText), 0);
    lv_label_set_text(nameLabel_, "no strip");
    lv_obj_align(nameLabel_, LV_ALIGN_TOP_MID, 0, scalePx(34));

    badge_ = lv_obj_create(root_);
    lv_obj_remove_style_all(badge_);
    lv_obj_set_size(badge_, scalePx(8), scalePx(8));
    lv_obj_set_style_radius(badge_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(badge_, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(badge_, lv_color_hex(kOffline), 0);
    lv_obj_align(badge_, LV_ALIGN_TOP_MID, 0, scalePx(24));

    hintLabel_ = lv_label_create(root_);
    lv_obj_set_width(hintLabel_, w - 2 * scalePx(28));
    lv_label_set_long_mode(hintLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hintLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hintLabel_, lv_color_hex(kHint), 0);
    lv_label_set_text(hintLabel_, "Tap power  •  Knob brightness  •  Swipe effect\nRing colour  •  Hold 3 s back");
    lv_obj_align(hintLabel_, LV_ALIGN_BOTTOM_MID, 0, -scalePx(18));

    promptLabel_ = lv_label_create(root_);
    lv_obj_set_width(promptLabel_, w - 2 * scalePx(40));
    lv_label_set_long_mode(promptLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(promptLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(promptLabel_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(promptLabel_, lv_color_hex(kDimGrey), 0);
    lv_obj_set_style_bg_opa(promptLabel_, LV_OPA_80, 0);
    lv_obj_set_style_pad_all(promptLabel_, scalePx(6), 0);
    lv_label_set_text(promptLabel_, "");
    lv_obj_align(promptLabel_, LV_ALIGN_CENTER, 0, scalePx(60));
    lv_obj_add_flag(promptLabel_, LV_OBJ_FLAG_HIDDEN);

    last_ = Rendered{};
    LvglDisplay::invalidateScreen();
}

void LightView::doDestroy() {
    if (root_ != nullptr) {
        lv_obj_del(root_);
    }
    root_ = fill_ = levelLabel_ = effectLabel_ = nameLabel_ = badge_ = hintLabel_ = promptLabel_ = nullptr;
    for (lv_obj_t*& arc : rimArcs_) arc = nullptr;
    last_ = Rendered{};
}

void LightView::doSetVisible(bool visible) {
    if (root_ == nullptr) return;
    if (visible) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void LightView::renderFill(const model::LightState& light) {
    const int w = panelWidth();
    const int h = panelHeight();
    const int fillHeight = (static_cast<int>(light.brightness) * h) / 100;
    lv_obj_set_size(fill_, w, fillHeight);
    lv_obj_set_pos(fill_, 0, h - fillHeight);
    lv_obj_set_style_bg_color(fill_, lv_color_hex(light.rgb & 0xFFFFFFu), 0);
    lv_obj_set_style_bg_opa(fill_, light.power ? LV_OPA_COVER : LV_OPA_30, 0);
    char text[16];
    std::snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(light.brightness));
    lv_label_set_text(levelLabel_, text);
    lv_label_set_text(effectLabel_, light.power ? model::ledEffectName(light.effect) : "off");
}

void LightView::renderLabels(const LightScreenState& state) {
    const model::LedRegistryState& reg = *state.registry;
    const int sel = reg.selected;
    const bool known = sel >= 0 && sel < static_cast<int>(model::LedRegistryState::kSlots) && reg.slots[sel].used;
    if (!known) {
        lv_label_set_text(nameLabel_, reg.used == 0 ? "no strip" : "choose a strip on the rim");
        lv_obj_set_style_bg_color(badge_, lv_color_hex(kOffline), 0);
        return;
    }
    const model::LedSlot& slot = reg.slots[sel];
    lv_label_set_text(nameLabel_, slot.name.empty() ? slot.id.c_str() : slot.name.c_str());
    lv_obj_set_style_bg_color(badge_, lv_color_hex(slot.online ? kOnline : kOffline), 0);
}

void LightView::renderRim(const model::LedRegistryState& reg) {
    const bool show = rim::chooserVisible(reg);
    const int count = static_cast<int>(reg.used);
    const float span = count > 0 ? 360.0f / static_cast<float>(count) : 0.0f;
    const float gap = count > 1 ? 4.0f : 0.0f;
    for (size_t i = 0; i < model::LedRegistryState::kSlots; ++i) {
        lv_obj_t* arc = rimArcs_[i];
        const model::LedSlot& slot = reg.slots[i];
        if (!show || !slot.used) {
            lv_obj_add_flag(arc, LV_OBJ_FLAG_HIDDEN);
            continue;
        }
        const int ordinal = rim::ordinalOfSlot(reg, static_cast<int>(i));
        const float start = static_cast<float>(ordinal) * span + gap / 2.0f;
        const float end = static_cast<float>(ordinal + 1) * span - gap / 2.0f;
        lv_arc_set_angles(arc, static_cast<uint16_t>(start), static_cast<uint16_t>(end));
        const bool selected = reg.selected == static_cast<int>(i);
        uint32_t colour = kGrey;
        if (slot.paired) {
            colour = slot.online ? (slot.state.rgb & 0xFFFFFFu) : kOffline;
        }
        lv_obj_set_style_arc_color(arc, lv_color_hex(colour), LV_PART_INDICATOR);
        lv_obj_set_style_arc_opa(arc, selected ? LV_OPA_COVER : LV_OPA_60, LV_PART_INDICATOR);
        lv_obj_clear_flag(arc, LV_OBJ_FLAG_HIDDEN);
    }
}

void LightView::renderPrompt(const model::LedRegistryState& reg, int dial) {
    char text[96];
    switch (reg.pairing) {
        case model::LedPairing::Requested:
            if (dial > 0) {
                std::snprintf(text, sizeof(text), "Count the blinks: %d  \xE2\x80\xA2  tap to confirm", dial);
            } else {
                std::snprintf(text, sizeof(text), "Count the blinks, turn the knob to that number");
            }
            break;
        case model::LedPairing::Confirming:
            std::snprintf(text, sizeof(text), "Pairing...");
            break;
        case model::LedPairing::Paired:
            std::snprintf(text, sizeof(text), "Paired");
            break;
        case model::LedPairing::Failed:
            std::snprintf(text, sizeof(text), "Pairing failed  \xE2\x80\xA2  hold the segment to retry");
            break;
        default:
            lv_obj_add_flag(promptLabel_, LV_OBJ_FLAG_HIDDEN);
            return;
    }
    lv_label_set_text(promptLabel_, text);
    lv_obj_clear_flag(promptLabel_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_align(promptLabel_, LV_ALIGN_CENTER, 0, scalePx(60));
}

void LightView::doRender(const LightScreenState& state) {
    if (state.light == nullptr || state.registry == nullptr || root_ == nullptr) {
        return;
    }
    const model::LightState& light = *state.light;
    const model::LedRegistryState& reg = *state.registry;
    Rendered now;
    now.light = light;
    now.used = reg.used;
    now.selected = reg.selected;
    for (size_t i = 0; i < model::LedRegistryState::kSlots; ++i) {
        now.usedSlot[i] = reg.slots[i].used;
        now.paired[i] = reg.slots[i].paired;
        now.online[i] = reg.slots[i].online;
        now.rgb[i] = reg.slots[i].state.rgb;
    }
    if (reg.selected >= 0 && reg.selected < static_cast<int>(model::LedRegistryState::kSlots)) {
        now.name = reg.slots[reg.selected].name;
    }
    now.pairing = reg.pairing;
    now.dial = state.dial;
    now.valid = true;

    const bool fillChanged = !last_.valid || last_.light != now.light;
    bool rimChanged = !last_.valid || last_.used != now.used || last_.selected != now.selected;
    for (size_t i = 0; i < model::LedRegistryState::kSlots && !rimChanged; ++i) {
        rimChanged = last_.usedSlot[i] != now.usedSlot[i] || last_.paired[i] != now.paired[i] ||
                     last_.online[i] != now.online[i] || last_.rgb[i] != now.rgb[i];
    }
    const bool labelsChanged = rimChanged || last_.name != now.name;
    const bool promptChanged = !last_.valid || last_.pairing != now.pairing || last_.dial != now.dial;

    if (fillChanged) renderFill(light);
    if (labelsChanged) renderLabels(state);
    if (rimChanged) renderRim(reg);
    if (promptChanged) renderPrompt(reg, state.dial);
    last_ = now;
}

}  // namespace ui
