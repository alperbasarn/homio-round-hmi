#pragma once
// KnobBindingMachine (MEDIA-14, the base of UIX-26): what the knob does on the MEDIA screen, as a
// pure state machine. VOLUME turns every detent into a SetVolume; SEEK_IDLE waits for the first
// detent; SEEK_ACCUM gathers detents into one scrub position and commits it as a single SeekTo on
// the release that ends the scrub (consumed, so it is not a play/pause tap) or after 450 ms
// idle; an idle SEEK_IDLE reverts to VOLUME after 6 s; back discards. Fine seek moves 1 % per
// detent; the coarse PC path moves one 10 % step per detent and snaps to that grid. Every call
// takes a snapshot of the media state and the clock; nothing here references a model, LVGL or ESP.
#include <cstdint>

#include "model/MediaModel.h"

namespace controllers {

enum class KnobBindingState : uint8_t { Volume = 0, SeekIdle, SeekAccum };

struct KnobAction {
    enum class Kind : uint8_t { None = 0, SetVolume, SeekTo, Scrubbing, BindingChanged };
    Kind kind = Kind::None;
    int value = 0;  // percent for SetVolume / SeekTo / Scrubbing; 1 = seek, 0 = volume for BindingChanged
};

class KnobBindingMachine {
public:
    static constexpr uint32_t kCommitIdleMs = 450;
    static constexpr uint32_t kSeekRevertMs = 6000;

    KnobBindingState state() const { return state_; }
    bool seeking() const { return state_ != KnobBindingState::Volume; }
    int scrubPercent() const { return state_ == KnobBindingState::SeekAccum ? scrub_ : -1; }

    // Enter seek when the snapshot allows it (a known duration and a seek resolution).
    KnobAction enterSeek(const model::MediaState& media, uint32_t nowMs);
    // Leave seek without committing (back, or the screen leaving).
    KnobAction exitSeek(uint32_t nowMs);
    KnobAction onDetents(int detents, const model::MediaState& media, uint32_t nowMs);
    // A touch release: true when it committed a scrub (the caller drops the tap).
    bool onRelease(KnobAction& out, uint32_t nowMs);
    // Once per frame: the idle commit and the idle revert.
    KnobAction tick(uint32_t nowMs);
    void reset();

private:
    static int positionPercent(const model::MediaState& media);
    KnobAction commit();

    KnobBindingState state_ = KnobBindingState::Volume;
    int scrub_ = 0;
    int step_ = 1;
    uint32_t lastDetentMs_ = 0;
    uint32_t enteredMs_ = 0;
};

}  // namespace controllers
