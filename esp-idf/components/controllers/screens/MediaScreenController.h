#pragma once
// MediaScreenController (MEDIA-13): the MEDIA screen. Ports the old sound controller state machine
// (initialise, wait for sync, update) as a phase with the timeout the old code never had: with no
// broker the screen still renders after 3 s, the knob still moves the volume, and a tap logs the
// refusal instead of wedging. Every transport call goes through the target registry and the
// screen never names a target; every value the screen owns reaches the model as a record.
//
//   knob        VOLUME: 1 % per detent (VolumeLocal + target.setVolume); SEEK: the binding machine
//   tap         toggle play/pause (target.toggle); while scrubbing, the tap commits the seek
//   double tap  enter or leave SEEK when the target can seek
//   swipe L/R   next / previous track (D14)
//   hold 3 s    back to INFO (a scrub in progress is discarded)
#include <cstdint>

#include "IScreenView.h"
#include "KnobBindingMachine.h"
#include "MediaScreenState.h"
#include "ScreenController.h"
#include "input/InputEvent.h"
#include "media_target/MediaTargetRegistry.h"
#include "model/AppModel.h"

namespace controllers {

class MediaScreenController final : public ScreenController {
public:
    static constexpr uint32_t kSyncTimeoutMs = 3000;
    static constexpr uint32_t kFlashMs = 1200;

    MediaScreenController(model::AppModel& app, media::MediaTargetRegistry& targets,
                          ui::ScreenView<ui::MediaScreenState>* view, int panelWidthPx, int panelHeightPx);

    model::AppScreen screen() const override { return model::AppScreen::Media; }
    void onEnter(uint32_t nowMs) override;
    void onExit(uint32_t nowMs) override;
    bool onInput(const input::InputEvent& event, uint32_t nowMs) override;
    void tick(uint32_t nowMs) override;

    ui::MediaPhase phase() const { return phase_; }
    const KnobBindingMachine& binding() const { return binding_; }
    uint32_t refusals() const { return refusals_; }

private:
    void post(const model::ModelUpdate& record);
    media::IMediaTarget& target();
    void act(const KnobAction& action, uint32_t nowMs);
    void noteResult(media::MediaError error, uint32_t nowMs);

    model::AppModel& app_;
    media::MediaTargetRegistry& targets_;
    ui::ScreenView<ui::MediaScreenState>* view_;
    int panelWidthPx_;
    int panelHeightPx_;
    KnobBindingMachine binding_;
    ui::MediaPhase phase_ = ui::MediaPhase::WaitForSync;
    uint32_t enteredMs_ = 0;
    uint32_t flashUntilMs_ = 0;
    input::HitZone pressZone_ = input::HitZone::Dead;
    uint32_t refusals_ = 0;
};

}  // namespace controllers
