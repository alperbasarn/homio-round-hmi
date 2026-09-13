#include "BootScreenController.h"

namespace controllers {

BootScreenController::BootScreenController(ui::ScreenView<model::NavigationState>* view,
                                           model::MutationQueue* queue,
                                           const model::NavigationModel* navigation, int panelWidthPx,
                                           int panelHeightPx)
    : view_(view),
      queue_(queue),
      navigation_(navigation),
      panelWidthPx_(panelWidthPx),
      panelHeightPx_(panelHeightPx) {}

void BootScreenController::onEnter(uint32_t nowMs) {
    if (entered_) {
        return;  // one start stamp: a second enter while active changes nothing (DISP-09)
    }
    entered_ = true;
    startMs_ = nowMs;
    progressPct_ = -1;
    complete_ = false;
    completeAtMs_ = 0;
    requested_ = false;
    cancelNavigationRequest();
    if (view_ != nullptr) {
        view_->build(nullptr, panelWidthPx_, panelHeightPx_);
        view_->setVisible(true);
    }
}

void BootScreenController::onExit(uint32_t /*nowMs*/) {
    entered_ = false;
    if (view_ != nullptr) {
        view_->destroy();
    }
}

void BootScreenController::tick(uint32_t nowMs) {
    if (!entered_) {
        return;
    }
    const uint32_t elapsed = nowMs >= startMs_ ? nowMs - startMs_ : 0;
    const int pct = elapsed >= kRampMs
                        ? 100
                        : static_cast<int>((static_cast<uint64_t>(elapsed) * 100u) / kRampMs);
    if (pct > progressPct_) {
        progressPct_ = pct;
        if (queue_ != nullptr) {
            queue_->post(model::NavigationModel::make(model::NavigationModel::Field::InitProgress, pct, nowMs));
        }
        if (pct >= 100 && !complete_) {
            complete_ = true;
            completeAtMs_ = nowMs;
        }
    }
    if (view_ != nullptr && navigation_ != nullptr) {
        view_->render(navigation_->state());
    }
    if (complete_ && !requested_ && nowMs - completeAtMs_ >= kHoldMs) {
        requested_ = true;
        requestScreen(model::AppScreen::Info, model::TransitionType::Immediate);
    }
}

}  // namespace controllers
