#include "model/MediaModel.h"

namespace model {

namespace {

bool validPercent(int value) {
    return value >= 0 && value <= 100;
}

}  // namespace

bool MediaState::operator==(const MediaState& other) const {
    return activeTarget == other.activeTarget && volume == other.volume && isPlaying == other.isPlaying &&
           stateSynced == other.stateSynced && knobBinding == other.knobBinding &&
           seekResolution == other.seekResolution && seekStepPercent == other.seekStepPercent &&
           pendingSeekPercent == other.pendingSeekPercent && availability[0] == other.availability[0] &&
           availability[1] == other.availability[1] && positionMs == other.positionMs &&
           durationMs == other.durationMs && positionSampledAtMs == other.positionSampledAtMs &&
           lastStateMs == other.lastStateMs && track == other.track && targetLabel == other.targetLabel;
}

MediaModel::MediaModel() : ModelBase(), volumePublish_(kPublishIntervalMs), volumeEcho_(kEchoWindowMs) {}

RecordClass MediaModel::classOf(Field field) {
    switch (field) {
        case Field::Volume:
        case Field::Position:
        case Field::PendingSeek:
        case Field::StateSeen:
            return RecordClass::CoalesceLatest;
        case Field::IsPlaying:
        case Field::ActiveTarget:
        case Field::KnobBinding:
        case Field::Availability:
        case Field::StateSynced:
        case Field::SeekResolution:
        case Field::Count:
        default:
            return RecordClass::NeverDrop;
    }
}

ModelUpdate MediaModel::make(Field field, int32_t value, uint32_t atMs, int32_t aux) {
    return ModelUpdate::ofInt(Aggregate::Media, static_cast<uint8_t>(field), classOf(field), value, atMs, aux);
}

bool MediaModel::applyUpdate(const ModelUpdate& update, Millis now) {
    switch (static_cast<Field>(update.field)) {
        case Field::Volume:
            return setVolumeFromRemote(update.value, now);
        case Field::IsPlaying:
            return setPlaying(update.asBool(), now);
        case Field::ActiveTarget:
            return setActiveTarget(static_cast<MediaTargetId>(update.value));
        case Field::KnobBinding:
            return setKnobBinding(static_cast<KnobBinding>(update.value));
        case Field::Position:
            return setPosition(update.value, update.aux, now);
        case Field::Availability:
            return setAvailability(static_cast<MediaTargetId>(update.value), static_cast<MediaAvailability>(update.aux));
        case Field::StateSynced:
            return setStateSynced(update.asBool());
        case Field::PendingSeek:
            return setPendingSeekPercent(update.value);
        case Field::SeekResolution:
            return setSeekResolution(static_cast<SeekResolution>(update.value));
        case Field::StateSeen:
            noteStateSeen(now);
            return true;
        case Field::Count:
        default:
            return false;
    }
}

bool MediaModel::setVolume(int percent) {
    if (!validPercent(percent)) {
        return false;
    }
    mutate().volume = static_cast<Percent>(percent);
    return true;
}

bool MediaModel::setVolumeFromRemote(int percent, Millis now) {
    if (!validPercent(percent)) {
        return false;
    }
    if (isVolumeEcho(percent, now)) {
        return false;
    }
    mutate().volume = static_cast<Percent>(percent);
    return true;
}

bool MediaModel::isVolumeEcho(int percent, Millis now) const {
    return volumeEcho_.isEcho(percent, now);
}

void MediaModel::markVolumeChanged() {
    volumePublish_.markChanged();
}

bool MediaModel::shouldPublishVolume(Millis now) {
    if (!volumePublish_.shouldPublish(now)) {
        return false;
    }
    volumeEcho_.published(state().volume, now);
    return true;
}

bool MediaModel::flushVolumePublish(Millis now) {
    if (!volumePublish_.flush()) {
        return false;
    }
    volumeEcho_.published(state().volume, now);
    return true;
}

bool MediaModel::setPlaying(bool playing, Millis now) {
    MediaState& s = mutate();
    s.isPlaying = playing;
    s.stateSynced = true;
    s.lastStateMs = now;
    return true;
}

bool MediaModel::setStateSynced(bool synced) {
    mutate().stateSynced = synced;
    return true;
}

void MediaModel::noteStateSeen(Millis now) {
    mutate().lastStateMs = now;
}

bool MediaModel::isStateStale(Millis now) const {
    const Millis last = state().lastStateMs;
    if (last < 0) {
        return true;
    }
    return now - last >= kStateStaleMs;
}

bool MediaModel::setActiveTarget(MediaTargetId target) {
    if (target >= MediaTargetId::Count) {
        return false;
    }
    MediaState& s = mutate();
    if (s.activeTarget == target) {
        return true;
    }
    s.activeTarget = target;
    s.stateSynced = false;
    s.isPlaying = false;
    s.knobBinding = KnobBinding::Volume;
    s.seekResolution = SeekResolution::None;
    s.seekStepPercent = 0;
    s.pendingSeekPercent = static_cast<int8_t>(kNoSeek);
    s.positionMs = 0;
    s.durationMs = 0;
    s.positionSampledAtMs = -1;
    s.lastStateMs = -1;
    s.track = TrackInfo{};
    s.targetLabel.clear();
    return true;
}

bool MediaModel::setKnobBinding(KnobBinding binding) {
    if (binding != KnobBinding::Volume && binding != KnobBinding::Seek) {
        return false;
    }
    if (binding == KnobBinding::Seek && !canSeek()) {
        return false;
    }
    mutate().knobBinding = binding;
    return true;
}

bool MediaModel::setTrack(const char* title, const char* artist, const char* album, const char* id) {
    TrackInfo next;
    next.title.assign(title);
    next.artist.assign(artist);
    next.album.assign(album);
    next.id.assign(id);
    if (next == state().track) {
        return false;
    }
    mutate().track = next;
    return true;
}

bool MediaModel::setTargetLabel(const char* label) {
    FixedString<24> next;
    next.assign(label);
    if (next == state().targetLabel) {
        return false;
    }
    mutate().targetLabel = next;
    return true;
}

bool MediaModel::setPosition(int32_t positionMs, int32_t durationMs, Millis now) {
    if (positionMs < 0 || durationMs < 0) {
        return false;
    }
    if (durationMs > 0 && positionMs > durationMs) {
        positionMs = durationMs;
    }
    MediaState& s = mutate();
    s.positionMs = positionMs;
    s.durationMs = durationMs;
    s.positionSampledAtMs = now;
    if (s.durationMs == 0 && s.knobBinding == KnobBinding::Seek) {
        s.knobBinding = KnobBinding::Volume;  // SEEK needs a known duration
        s.pendingSeekPercent = static_cast<int8_t>(kNoSeek);
    }
    return true;
}

int32_t MediaModel::estimatedPositionMs(Millis now) const {
    const MediaState& s = state();
    if (s.positionSampledAtMs < 0) {
        return -1;
    }
    int64_t estimate = s.positionMs;
    if (s.isPlaying && now > s.positionSampledAtMs) {
        estimate += now - s.positionSampledAtMs;
    }
    if (s.durationMs > 0 && estimate > s.durationMs) {
        estimate = s.durationMs;
    }
    return static_cast<int32_t>(estimate);
}

bool MediaModel::availabilityValidFor(MediaTargetId target, MediaAvailability state) {
    switch (target) {
        case MediaTargetId::Spotify:
            return state == MediaAvailability::Ok || state == MediaAvailability::NoToken ||
                   state == MediaAvailability::NoPremium || state == MediaAvailability::Offline ||
                   state == MediaAvailability::Syncing;
        case MediaTargetId::Pc:
            return state == MediaAvailability::Ok || state == MediaAvailability::PcOffline ||
                   state == MediaAvailability::NoPlayer || state == MediaAvailability::NoMqtt ||
                   state == MediaAvailability::Stale;
        case MediaTargetId::Count:
        default:
            return false;
    }
}

bool MediaModel::setAvailability(MediaTargetId target, MediaAvailability availability) {
    if (!availabilityValidFor(target, availability)) {
        return false;
    }
    mutate().availability[static_cast<size_t>(target)] = availability;
    return true;
}

MediaAvailability MediaModel::availabilityOf(MediaTargetId target) const {
    if (target >= MediaTargetId::Count) {
        return MediaAvailability::Count;
    }
    return state().availability[static_cast<size_t>(target)];
}

bool MediaModel::activeTargetAvailable() const {
    return availabilityOf(state().activeTarget) == MediaAvailability::Ok;
}

bool MediaModel::setPendingSeekPercent(int percent) {
    if (percent != kNoSeek && !validPercent(percent)) {
        return false;
    }
    if (percent != kNoSeek && !canSeek()) {
        return false;
    }
    mutate().pendingSeekPercent = static_cast<int8_t>(percent);
    return true;
}

int MediaModel::takePendingSeek() {
    const int pending = state().pendingSeekPercent;
    if (pending < 0) {
        return kNoSeek;
    }
    mutate().pendingSeekPercent = static_cast<int8_t>(kNoSeek);
    return pending;
}

bool MediaModel::setSeekResolution(SeekResolution resolution) {
    uint8_t step = 0;
    switch (resolution) {
        case SeekResolution::None: step = 0; break;
        case SeekResolution::Coarse: step = 10; break;
        case SeekResolution::Fine: step = 1; break;
        default: return false;
    }
    MediaState& s = mutate();
    s.seekResolution = resolution;
    s.seekStepPercent = step;
    if (resolution == SeekResolution::None && s.knobBinding == KnobBinding::Seek) {
        s.knobBinding = KnobBinding::Volume;
        s.pendingSeekPercent = static_cast<int8_t>(kNoSeek);
    }
    return true;
}

bool MediaModel::canSeek() const {
    const MediaState& s = state();
    return s.seekResolution != SeekResolution::None && s.durationMs > 0;
}

}  // namespace model
