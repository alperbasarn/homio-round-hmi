#pragma once
// BootScreenController (SCRN-08, folds DISP-09): the boot ramp. One start stamp at onEnter, one
// ramp 0..100 over kRampMs posted to the model as InitProgress records (the model refuses a
// backward step), the view rendered from the model state, and one Info request kHoldMs after the
// ramp completes. The host (DisplayController now, the router in Phase 4) decides when that
// request may be honoured; until then it stays pending. Nothing here touches LVGL: the view does,
// under the lock the host holds around onEnter, onExit and tick.
#include <cstdint>

#include "IScreenView.h"
#include "ScreenController.h"
#include "model/MutationQueue.h"
#include "model/NavigationModel.h"

namespace controllers {

class BootScreenController final : public ScreenController {
public:
    static constexpr uint32_t kRampMs = 2500;
    static constexpr uint32_t kHoldMs = 400;

    // view may be null (host tests); queue and navigation outlive the controller.
    BootScreenController(ui::ScreenView<model::NavigationState>* view, model::MutationQueue* queue,
                         const model::NavigationModel* navigation, int panelWidthPx, int panelHeightPx);

    model::AppScreen screen() const override { return model::AppScreen::Boot; }
    void onEnter(uint32_t nowMs) override;
    void onExit(uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;

    bool active() const { return entered_; }
    int progressPercent() const { return progressPct_ < 0 ? 0 : progressPct_; }
    bool rampComplete() const { return complete_; }

private:
    ui::ScreenView<model::NavigationState>* view_;
    model::MutationQueue* queue_;
    const model::NavigationModel* navigation_;
    int panelWidthPx_;
    int panelHeightPx_;
    bool entered_ = false;
    uint32_t startMs_ = 0;
    int progressPct_ = -1;  // the last value posted; -1 until the first tick
    bool complete_ = false;
    uint32_t completeAtMs_ = 0;
    bool requested_ = false;
};

}  // namespace controllers
