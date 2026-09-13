#pragma once
// MediaView (CTRL-15, first cut): the MEDIA screen. R1 is the volume ring at the design's
// geometry (420 px of 466, 25 px wide, rotation 120, 300 degrees), R2 the timeline (21 px, inside
// R1, absent while the duration is unknown, showing the scrub position while the knob seeks), the
// disc carries the percent, the play state, the target eyebrow and the scrolling title, and a
// status line names the sync phase or a refused control. Renders only what changed.
#include "IScreenView.h"
#include "MediaScreenState.h"

namespace ui {

class MediaView final : public ScreenView<MediaScreenState> {
public:
    lv_obj_t* root() const override { return root_; }

protected:
    void doBuild(lv_obj_t* parent) override;
    void doDestroy() override;
    void doSetVisible(bool visible) override;
    void doRender(const MediaScreenState& state) override;

private:
    struct Rendered {
        model::MediaState media;
        MediaPhase phase = MediaPhase::WaitForSync;
        bool available = false;
        model::FixedString<24> label;
        int scrub = -1;
        bool flash = false;
        bool valid = false;
    };
    void renderRing(const model::MediaState& media, MediaPhase phase);
    void renderTimeline(const model::MediaState& media, int scrub);
    void renderText(const MediaScreenState& state);
    void renderStatus(const MediaScreenState& state);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* ring_ = nullptr;
    lv_obj_t* timeline_ = nullptr;
    lv_obj_t* levelLabel_ = nullptr;
    lv_obj_t* stateLabel_ = nullptr;
    lv_obj_t* eyebrowLabel_ = nullptr;
    lv_obj_t* titleLabel_ = nullptr;
    lv_obj_t* statusLabel_ = nullptr;
    lv_obj_t* hintLabel_ = nullptr;
    Rendered last_;
};

}  // namespace ui
