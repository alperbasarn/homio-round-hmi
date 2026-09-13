#include "MediaView.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "LvglDisplay.h"
#include "sdkconfig.h"

namespace ui {

namespace {
constexpr uint32_t kRingTrack = 0x122238u;
constexpr uint32_t kRingLive = 0x39D9FFu;
constexpr uint32_t kRingDim = 0x1E6F85u;
constexpr uint32_t kTimelineTrack = 0x1B2030u;
constexpr uint32_t kTimeline = 0xB8C5D8u;
constexpr uint32_t kScrub = 0xFFFFFFu;
constexpr uint32_t kText = 0xDDE7F5u;
constexpr uint32_t kMuted = 0x8A96AAu;
constexpr uint32_t kHint = 0x6B778Au;
constexpr uint32_t kWarn = 0xFFAA00u;

const char* availabilityText(const model::MediaState& m) {
    switch (m.availability[static_cast<size_t>(m.activeTarget)]) {
        case model::MediaAvailability::Ok: return "";
        case model::MediaAvailability::NoToken: return "Spotify: not signed in";
        case model::MediaAvailability::NoPremium: return "Spotify: premium required";
        case model::MediaAvailability::Offline: return "Spotify: offline";
        case model::MediaAvailability::Syncing: return "Spotify: syncing";
        case model::MediaAvailability::PcOffline: return "PC not answering";
        case model::MediaAvailability::NoPlayer: return "PC: no player";
        case model::MediaAvailability::NoMqtt: return "No broker";
        case model::MediaAvailability::Stale: return "PC: stale";
        default: return "";
    }
}

lv_obj_t* makeArc(lv_obj_t* parent, int diameter, int width, uint32_t track, uint32_t indicator) {
    lv_obj_t* arc = lv_arc_create(parent);
    lv_obj_remove_style_all(arc);
    lv_obj_set_size(arc, diameter, diameter);
    lv_obj_center(arc);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, width, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, width, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(track), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(indicator), LV_PART_INDICATOR);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_arc_opa(arc, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_MAIN);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(arc, LV_OPA_TRANSP, LV_PART_KNOB);
    lv_arc_set_mode(arc, LV_ARC_MODE_NORMAL);
    lv_arc_set_rotation(arc, 120);      // CTRL-15: the design's R1, 300 degrees open at the bottom
    lv_arc_set_bg_angles(arc, 0, 300);
    lv_arc_set_range(arc, 0, 100);
    lv_arc_set_value(arc, 0);
    return arc;
}
}  // namespace

void MediaView::doBuild(lv_obj_t* parent) {
    const int w = panelWidth();
    const int h = panelHeight();
    const int side = std::min(w, h);
    root_ = lv_obj_create(parent != nullptr ? parent : lv_scr_act());
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, w, h);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root_, LV_OBJ_FLAG_SCROLLABLE);

    ring_ = makeArc(root_, (420 * side) / 466, std::max(6, (25 * side) / 466), kRingTrack, kRingLive);
    timeline_ = makeArc(root_, (354 * side) / 466, std::max(4, (21 * side) / 466), kTimelineTrack, kTimeline);
    lv_obj_add_flag(timeline_, LV_OBJ_FLAG_HIDDEN);

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
    eyebrowLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_color(eyebrowLabel_, lv_color_hex(kMuted), 0);
    lv_label_set_text(eyebrowLabel_, "PC");
    lv_obj_align(eyebrowLabel_, LV_ALIGN_CENTER, 0, -scalePx(52));

    levelLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_font(levelLabel_, bigFont, 0);
    lv_obj_set_style_text_color(levelLabel_, lv_color_white(), 0);
    lv_label_set_text(levelLabel_, "--%");
    lv_obj_align(levelLabel_, LV_ALIGN_CENTER, 0, -scalePx(12));

    stateLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_font(stateLabel_, textFont, 0);
    lv_obj_set_style_text_color(stateLabel_, lv_color_hex(kText), 0);
    lv_label_set_text(stateLabel_, "");
    lv_obj_align(stateLabel_, LV_ALIGN_CENTER, 0, scalePx(22));

    titleLabel_ = lv_label_create(root_);
    lv_obj_set_width(titleLabel_, (300 * side) / 466);
    lv_label_set_long_mode(titleLabel_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_obj_set_style_text_align(titleLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(titleLabel_, lv_color_hex(kMuted), 0);
    lv_label_set_text(titleLabel_, "");
    lv_obj_align(titleLabel_, LV_ALIGN_CENTER, 0, scalePx(46));

    statusLabel_ = lv_label_create(root_);
    lv_obj_set_style_text_color(statusLabel_, lv_color_hex(kWarn), 0);
    lv_label_set_text(statusLabel_, "");
    lv_obj_align(statusLabel_, LV_ALIGN_CENTER, 0, scalePx(68));

    hintLabel_ = lv_label_create(root_);
    lv_obj_set_width(hintLabel_, w - 2 * scalePx(28));
    lv_label_set_long_mode(hintLabel_, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(hintLabel_, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(hintLabel_, lv_color_hex(kHint), 0);
    lv_label_set_text(hintLabel_, "Tap play/pause  \xE2\x80\xA2  Knob volume  \xE2\x80\xA2  Swipe track\nDouble-tap seek  \xE2\x80\xA2  Hold 3 s back");
    lv_obj_align(hintLabel_, LV_ALIGN_BOTTOM_MID, 0, -scalePx(14));

    last_ = Rendered{};
    LvglDisplay::invalidateScreen();
}

void MediaView::doDestroy() {
    if (root_ != nullptr) {
        lv_obj_del(root_);
    }
    root_ = ring_ = timeline_ = levelLabel_ = stateLabel_ = eyebrowLabel_ = titleLabel_ = statusLabel_ = hintLabel_ = nullptr;
    last_ = Rendered{};
}

void MediaView::doSetVisible(bool visible) {
    if (root_ == nullptr) return;
    if (visible) lv_obj_clear_flag(root_, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(root_, LV_OBJ_FLAG_HIDDEN);
}

void MediaView::renderRing(const model::MediaState& media, MediaPhase phase) {
    lv_arc_set_value(ring_, static_cast<int16_t>(media.volume));
    lv_obj_set_style_arc_color(ring_, lv_color_hex(phase == MediaPhase::Live ? kRingLive : kRingDim), LV_PART_INDICATOR);
    char text[16];
    std::snprintf(text, sizeof(text), "%u%%", static_cast<unsigned>(media.volume));
    lv_label_set_text(levelLabel_, text);
}

void MediaView::renderTimeline(const model::MediaState& media, int scrub) {
    if (media.durationMs <= 0) {
        lv_obj_add_flag(timeline_, LV_OBJ_FLAG_HIDDEN);  // UIX-19 ABSENT: no cap, no bar
        return;
    }
    int percent = static_cast<int>((static_cast<int64_t>(media.positionMs) * 100) / media.durationMs);
    if (scrub >= 0) {
        percent = scrub;
    }
    if (percent < 0) percent = 0;
    if (percent > 100) percent = 100;
    lv_arc_set_value(timeline_, static_cast<int16_t>(percent));
    lv_obj_set_style_arc_color(timeline_, lv_color_hex(scrub >= 0 ? kScrub : kTimeline), LV_PART_INDICATOR);
    lv_obj_clear_flag(timeline_, LV_OBJ_FLAG_HIDDEN);
}

void MediaView::renderText(const MediaScreenState& state) {
    const model::MediaState& m = *state.media;
    lv_label_set_text(eyebrowLabel_, (state.targetLabel != nullptr && state.targetLabel[0] != '\0') ? state.targetLabel : "PC");
    if (state.scrubPercent >= 0) {
        char text[24];
        std::snprintf(text, sizeof(text), "Seek %d%%", state.scrubPercent);
        lv_label_set_text(stateLabel_, text);
    } else if (m.knobBinding == model::KnobBinding::Seek) {
        lv_label_set_text(stateLabel_, "Knob: seek");
    } else {
        lv_label_set_text(stateLabel_, m.isPlaying ? "Playing" : "Paused");
    }
    lv_label_set_text(titleLabel_, m.track.title.c_str());
}

void MediaView::renderStatus(const MediaScreenState& state) {
    const char* text = "";
    if (state.unavailableFlash) {
        text = "Not available";
    } else if (state.phase == MediaPhase::WaitForSync) {
        text = "Syncing...";
    } else if (state.phase == MediaPhase::Unsynced || !state.targetAvailable) {
        text = availabilityText(*state.media);
        if (text[0] == '\0') text = "No reply";
    }
    lv_label_set_text(statusLabel_, text);
}

void MediaView::doRender(const MediaScreenState& state) {
    if (state.media == nullptr || root_ == nullptr) {
        return;
    }
    Rendered now;
    now.media = *state.media;
    now.phase = state.phase;
    now.available = state.targetAvailable;
    now.label.assign(state.targetLabel);
    now.scrub = state.scrubPercent;
    now.flash = state.unavailableFlash;
    now.valid = true;

    const bool ringChanged = !last_.valid || last_.media.volume != now.media.volume || last_.phase != now.phase;
    const bool timelineChanged = !last_.valid || last_.media.positionMs != now.media.positionMs ||
                                 last_.media.durationMs != now.media.durationMs || last_.scrub != now.scrub;
    const bool textChanged = !last_.valid || last_.media.isPlaying != now.media.isPlaying ||
                             last_.media.knobBinding != now.media.knobBinding || last_.scrub != now.scrub ||
                             last_.media.track != now.media.track || last_.label != now.label;
    const bool statusChanged = !last_.valid || last_.phase != now.phase || last_.available != now.available ||
                               last_.flash != now.flash || last_.media.availability[0] != now.media.availability[0] ||
                               last_.media.availability[1] != now.media.availability[1];
    if (ringChanged) renderRing(now.media, now.phase);
    if (timelineChanged) renderTimeline(now.media, now.scrub);
    if (textChanged) renderText(state);
    if (statusChanged) renderStatus(state);
    last_ = now;
}

}  // namespace ui
