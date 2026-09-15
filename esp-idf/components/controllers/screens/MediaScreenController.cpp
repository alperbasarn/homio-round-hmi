#include "MediaScreenController.h"

namespace controllers {

using input::Gesture;
using input::HitZone;
using input::InputType;
using media::MediaError;
using model::MediaModel;

MediaScreenController::MediaScreenController(model::AppModel& app, media::MediaTargetRegistry& targets,
                                             ui::ScreenView<ui::MediaScreenState>* view, int panelWidthPx,
                                             int panelHeightPx)
    : app_(app), targets_(targets), view_(view), panelWidthPx_(panelWidthPx), panelHeightPx_(panelHeightPx) {}

void MediaScreenController::post(const model::ModelUpdate& record) {
    app_.mutations().post(record);
}

media::IMediaTarget& MediaScreenController::target() {
    return targets_.target(app_.media().state().activeTarget);
}

void MediaScreenController::onEnter(uint32_t nowMs) {
    cancelNavigationRequest();  // a back request the router never polled must not outlive the screen
    const model::MediaState& m = app_.media().state();
    phase_ = m.stateSynced ? ui::MediaPhase::Live : ui::MediaPhase::WaitForSync;
    enteredMs_ = nowMs;
    flashUntilMs_ = 0;
    pressZone_ = HitZone::Dead;
    binding_.reset();
    if (m.knobBinding != model::KnobBinding::Volume) {
        post(MediaModel::make(MediaModel::Field::KnobBinding, static_cast<int32_t>(model::KnobBinding::Volume), nowMs));
    }
    if (view_ != nullptr) {
        view_->build(nullptr, panelWidthPx_, panelHeightPx_);
        view_->setVisible(true);
    }
    // The old WAIT_FOR_SYNC: ask once; the phase moves on with the reply or the timeout.
    target().requestState(nowMs);
    // UIX-11: the knob context for the divisor. The push of the volume down to the knob waits for
    // the write path (WP3.6); a pending write with no sender would only drop every burst as stale.
    app_.knob().setContext(model::KnobContext::Volume);
}

void MediaScreenController::onExit(uint32_t nowMs) {
    cancelNavigationRequest();
    act(binding_.exitSeek(nowMs), nowMs);
    if (view_ != nullptr) {
        view_->destroy();
    }
}

bool MediaScreenController::onInput(const input::InputEvent& e, uint32_t nowMs) {
    switch (e.type) {
        case InputType::TouchPress:
            pressZone_ = e.zone;
            return true;

        case InputType::TouchMove:
            return true;

        case InputType::TouchRelease: {
            KnobAction commit;
            if (binding_.onRelease(commit, nowMs)) {
                act(commit, nowMs);  // the release that ends a scrub is the commit, not a tap
                return true;
            }
            if (pressZone_ == HitZone::Rim) {
                return true;  // reserved for the target overlay (UIX-28)
            }
            noteResult(target().toggle(nowMs), nowMs);
            return true;
        }

        case InputType::TouchHoldRelease:
            act(binding_.exitSeek(nowMs), nowMs);  // back discards a scrub (UIX-26)
            requestScreen(model::AppScreen::Info);
            return true;

        case InputType::Gesture:
            switch (e.gesture) {
                case Gesture::SwipeLeft:
                    noteResult(target().next(nowMs), nowMs);
                    break;
                case Gesture::SwipeRight:
                    noteResult(target().previous(nowMs), nowMs);
                    break;
                case Gesture::DoubleTap:
                    if (binding_.seeking()) {
                        act(binding_.exitSeek(nowMs), nowMs);
                    } else {
                        const KnobAction a = binding_.enterSeek(app_.media().state(), nowMs);
                        if (a.kind == KnobAction::Kind::None) {
                            noteResult(MediaError::NotSupported, nowMs);
                        } else {
                            act(a, nowMs);
                        }
                    }
                    break;
                default:
                    break;
            }
            return true;

        case InputType::KnobDelta:
            act(binding_.onDetents(e.knobDelta, app_.media().state(), nowMs), nowMs);
            return true;

        default:
            return false;
    }
}

void MediaScreenController::noteResult(MediaError error, uint32_t nowMs) {
    if (error == MediaError::Ok || error == MediaError::Busy) {
        return;  // Busy is the toggle suppression doing its job
    }
    ++refusals_;
    flashUntilMs_ = nowMs + kFlashMs;
}

void MediaScreenController::act(const KnobAction& a, uint32_t nowMs) {
    switch (a.kind) {
        case KnobAction::Kind::SetVolume:
            post(MediaModel::make(MediaModel::Field::VolumeLocal, a.value, nowMs));
            target().setVolume(a.value, nowMs);  // offline the knob still moves the ring
            break;
        case KnobAction::Kind::Scrubbing:
            post(MediaModel::make(MediaModel::Field::PendingSeek, a.value, nowMs));
            break;
        case KnobAction::Kind::SeekTo:
            noteResult(target().seekAbsolute(a.value, nowMs), nowMs);
            post(MediaModel::make(MediaModel::Field::PendingSeek, MediaModel::kNoSeek, nowMs));
            break;
        case KnobAction::Kind::BindingChanged:
            post(MediaModel::make(MediaModel::Field::KnobBinding,
                                  static_cast<int32_t>(a.value != 0 ? model::KnobBinding::Seek : model::KnobBinding::Volume),
                                  nowMs));
            if (a.value == 0) {
                post(MediaModel::make(MediaModel::Field::PendingSeek, MediaModel::kNoSeek, nowMs));
            }
            break;
        default:
            break;
    }
}

void MediaScreenController::tick(uint32_t nowMs) {
    const model::MediaState& m = app_.media().state();
    switch (phase_) {
        case ui::MediaPhase::WaitForSync:
            if (m.stateSynced) {
                phase_ = ui::MediaPhase::Live;
            } else if (nowMs - enteredMs_ >= kSyncTimeoutMs) {
                phase_ = ui::MediaPhase::Unsynced;  // the timeout the old sound controller never had
            }
            break;
        case ui::MediaPhase::Unsynced:
            if (m.stateSynced) {
                phase_ = ui::MediaPhase::Live;
            }
            break;
        case ui::MediaPhase::Live:
            if (!m.stateSynced) {  // the link dropped or the target changed: wait again
                phase_ = ui::MediaPhase::WaitForSync;
                enteredMs_ = nowMs;
            }
            break;
    }
    act(binding_.tick(nowMs), nowMs);
    if (view_ != nullptr) {
        ui::MediaScreenState s;
        s.media = &m;
        s.phase = phase_;
        s.targetAvailable = target().available();
        s.targetLabel = target().label();
        s.scrubPercent = binding_.scrubPercent();
        s.unavailableFlash = nowMs < flashUntilMs_;
        view_->render(s);
    }
}

}  // namespace controllers
