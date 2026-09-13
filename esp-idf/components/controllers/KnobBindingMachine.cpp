#include "KnobBindingMachine.h"

namespace controllers {

namespace {
int clampPercent(int v) { return v < 0 ? 0 : (v > 100 ? 100 : v); }
}  // namespace

int KnobBindingMachine::positionPercent(const model::MediaState& media) {
    if (media.durationMs <= 0) {
        return 0;
    }
    const int64_t pct = (static_cast<int64_t>(media.positionMs) * 100) / media.durationMs;
    return clampPercent(static_cast<int>(pct));
}

KnobAction KnobBindingMachine::enterSeek(const model::MediaState& media, uint32_t nowMs) {
    KnobAction a;
    if (seeking() || media.durationMs <= 0 || media.seekResolution == model::SeekResolution::None) {
        return a;
    }
    state_ = KnobBindingState::SeekIdle;
    step_ = media.seekStepPercent > 0 ? media.seekStepPercent : 1;
    enteredMs_ = nowMs;
    lastDetentMs_ = nowMs;
    a.kind = KnobAction::Kind::BindingChanged;
    a.value = 1;
    return a;
}

KnobAction KnobBindingMachine::exitSeek(uint32_t /*nowMs*/) {
    KnobAction a;
    if (!seeking()) {
        return a;
    }
    state_ = KnobBindingState::Volume;
    scrub_ = 0;
    a.kind = KnobAction::Kind::BindingChanged;
    a.value = 0;
    return a;
}

KnobAction KnobBindingMachine::onDetents(int detents, const model::MediaState& media, uint32_t nowMs) {
    KnobAction a;
    if (detents == 0) {
        return a;
    }
    lastDetentMs_ = nowMs;
    switch (state_) {
        case KnobBindingState::Volume:
            a.kind = KnobAction::Kind::SetVolume;
            a.value = clampPercent(static_cast<int>(media.volume) + detents);
            return a;
        case KnobBindingState::SeekIdle:
            scrub_ = positionPercent(media);
            if (step_ > 1) {
                scrub_ = (scrub_ / step_) * step_;  // start on the grid the target can reach
            }
            state_ = KnobBindingState::SeekAccum;
            [[fallthrough]];
        case KnobBindingState::SeekAccum:
            scrub_ = clampPercent(scrub_ + detents * step_);
            a.kind = KnobAction::Kind::Scrubbing;
            a.value = scrub_;
            return a;
        default:
            return a;
    }
}

KnobAction KnobBindingMachine::commit() {
    KnobAction a;
    a.kind = KnobAction::Kind::SeekTo;
    a.value = scrub_;
    state_ = KnobBindingState::SeekIdle;
    scrub_ = 0;
    return a;
}

bool KnobBindingMachine::onRelease(KnobAction& out, uint32_t nowMs) {
    if (state_ != KnobBindingState::SeekAccum) {
        out = KnobAction{};
        return false;
    }
    out = commit();
    lastDetentMs_ = nowMs;
    return true;
}

KnobAction KnobBindingMachine::tick(uint32_t nowMs) {
    KnobAction a;
    switch (state_) {
        case KnobBindingState::SeekAccum:
            if (nowMs - lastDetentMs_ >= kCommitIdleMs) {
                a = commit();
                lastDetentMs_ = nowMs;
            }
            return a;
        case KnobBindingState::SeekIdle:
            if (nowMs - lastDetentMs_ >= kSeekRevertMs) {
                return exitSeek(nowMs);
            }
            return a;
        default:
            return a;
    }
}

void KnobBindingMachine::reset() {
    state_ = KnobBindingState::Volume;
    scrub_ = 0;
    step_ = 1;
    lastDetentMs_ = 0;
    enteredMs_ = 0;
}

}  // namespace controllers
