#pragma once
// InitializationView (SCRN-07): the boot splash as a view. A progress arc (start 120 deg, span
// 300 deg, range 0..100) around a black disc carrying the red Q and the white NOB, rendered from
// NavigationState::initProgressPct and nothing else. The arc eases to each new target over 500 ms;
// that animation is presentation state and stays here. No timing, no model writes, no input.
#include "IScreenView.h"
#include "model/NavigationModel.h"

namespace ui {

class InitializationView final : public ScreenView<model::NavigationState> {
public:
    static constexpr float kArcStartDeg = 120.0f;
    static constexpr float kArcSpanDeg = 300.0f;
    static constexpr uint32_t kArcAnimationMs = 500;

    lv_obj_t* root() const override { return root_; }
    int shownPercent() const { return currentPct_; }  // where the arc is right now (animating)
    int targetPercent() const { return targetPct_; }

protected:
    void doBuild(lv_obj_t* parent) override;
    void doDestroy() override;
    void doSetVisible(bool visible) override;
    void doRender(const model::NavigationState& state) override;

private:
    static int16_t normalizeAngle(float degrees);
    static void arcAnimExec(void* var, int32_t value);
    void applyArcValue(int percent);

    lv_obj_t* root_ = nullptr;
    lv_obj_t* arc_ = nullptr;
    lv_obj_t* disc_ = nullptr;
    lv_obj_t* qLabel_ = nullptr;
    lv_obj_t* nobLabel_ = nullptr;
    int currentPct_ = 0;
    int targetPct_ = 0;
};

}  // namespace ui
