#include "media_target/PcMediaAdapter.h"

namespace media {

using model::MediaAvailability;
using model::MediaModel;

PcMediaAdapter::PcMediaAdapter(model::MutationQueue* queue, model::MediaModel* media, PublishFn publish, void* context)
    : queue_(queue),
      media_(media),
      publish_(publish),
      context_(context),
      volumeLimiter_(kVolumeWindowMs),
      settle_(kVolumeSettleMs) {}

void PcMediaAdapter::setLinkUp(bool up, model::Millis now) {
    if (linkUp_ == up) {
        return;
    }
    linkUp_ = up;
    controlSentMs_ = -1;
    if (up) {
        requestState(now);
        return;
    }
    synced_ = false;
    post(MediaModel::make(MediaModel::Field::Availability, static_cast<int32_t>(MediaTargetId::Pc),
                          static_cast<uint32_t>(now), static_cast<int32_t>(MediaAvailability::NoMqtt)));
    post(MediaModel::make(MediaModel::Field::StateSynced, 0, static_cast<uint32_t>(now)));
}

bool PcMediaAdapter::publish(const char* topic, const char* body) {
    if (publish_ == nullptr || !linkUp_) {
        return false;
    }
    if (!publish_(topic, body, context_)) {
        ++publishFailures_;
        return false;
    }
    ++publishes_;
    return true;
}

void PcMediaAdapter::post(const model::ModelUpdate& record) {
    if (queue_ != nullptr) {
        queue_->post(record);
    }
}

MediaCaps PcMediaAdapter::caps() const {
    MediaCaps c;
    c.volume = true;
    c.playPause = true;
    c.toggle = true;
    c.skip = true;
    c.seekAbsolute = v2Seen_;
    c.seekRelative = v2Seen_;
    c.position = v2Seen_;
    c.trackInfo = v2Seen_;
    c.seekStepPercent = v2Seen_ ? 10 : 0;  // the digit-key path is coarse
    return c;
}

// ---- volume (MEDIA-07) ----

MediaError PcMediaAdapter::setVolume(int percent, model::Millis now) {
    if (percent < 0 || percent > 100) {
        return MediaError::InvalidArgument;
    }
    if (!linkUp_) {
        return MediaError::NotAvailable;
    }
    pendingVolume_ = percent;
    volumeLimiter_.markChanged();
    settle_.poke(now);
    if (volumeLimiter_.shouldPublish(now)) {
        flushVolume(now);
    }
    return MediaError::Ok;
}

void PcMediaAdapter::flushVolume(model::Millis now) {
    if (pendingVolume_ < 0) {
        return;
    }
    char body[pc::kBodyCapacity];
    if (pc::encodeSetpoint(body, sizeof(body), pendingVolume_) < 0) {
        pendingVolume_ = -1;
        return;
    }
    if (!publish(pc::kTopicSetpoint, body)) {
        volumeLimiter_.markChanged();  // the next window or the settling call retries
        return;
    }
    lastSentVolume_ = pendingVolume_;
    lastVolumeSentMs_ = now;
    ++sentGeneration_;
    pendingVolume_ = -1;
}

void PcMediaAdapter::tick(model::Millis now) {
    if (pendingVolume_ >= 0 && volumeLimiter_.shouldPublish(now)) {
        flushVolume(now);
    }
    if (settle_.settled(now) && pendingVolume_ >= 0) {
        volumeLimiter_.flush();
        flushVolume(now);  // the value the knob settled on, whatever the window says
    }
}

// Our own setpoint comes back as a volume report. The value we last sent acknowledges the
// generation; any other value while a publish is in flight (or while the knob is still turning)
// is a stale echo, until the window says the publish was lost and the PC may really differ.
bool PcMediaAdapter::acceptRemoteVolume(int volume, model::Millis now) {
    if (volumeInFlight() && volume == lastSentVolume_) {
        ackedGeneration_ = sentGeneration_;
        ++echoesDropped_;
        return false;
    }
    if (pendingVolume_ >= 0) {
        ++echoesDropped_;
        return false;
    }
    if (volumeInFlight() && now - lastVolumeSentMs_ <= kEchoWindowMs) {
        ++echoesDropped_;
        return false;
    }
    return true;
}

// ---- transport controls (MEDIA-08, MEDIA-09, MEDIA-10) ----

MediaError PcMediaAdapter::control(pc::Control verb, bool playingAfter, model::Millis now) {
    if (!linkUp_) {
        return MediaError::NotAvailable;
    }
    char body[pc::kBodyCapacity];
    if (pc::encodeControl(body, sizeof(body), verb) < 0) {
        return MediaError::InvalidArgument;
    }
    if (!publish(pc::kTopicControl, body)) {
        return MediaError::Transport;
    }
    playing_ = playingAfter;  // optimistic; the reply is authoritative
    controlSentMs_ = now;
    return MediaError::Ok;
}

MediaError PcMediaAdapter::play(model::Millis now) {
    if (!linkUp_) return MediaError::NotAvailable;
    if (synced_ && playing_) return MediaError::Ok;  // idempotent: the PC's key would pause
    return control(pc::Control::Play, true, now);
}

MediaError PcMediaAdapter::pause(model::Millis now) {
    if (!linkUp_) return MediaError::NotAvailable;
    if (synced_ && !playing_) return MediaError::Ok;
    return control(pc::Control::Pause, false, now);
}

MediaError PcMediaAdapter::toggle(model::Millis now) {
    if (!linkUp_) return MediaError::NotAvailable;
    if (controlSentMs_ >= 0 && now - controlSentMs_ < kControlSuppressMs) return MediaError::Busy;
    if (v2Seen_) return control(pc::Control::Toggle, !playing_, now);
    return playing_ ? control(pc::Control::Pause, false, now) : control(pc::Control::Play, true, now);
}

MediaError PcMediaAdapter::next(model::Millis now) {
    if (!linkUp_) return MediaError::NotAvailable;
    return control(pc::Control::Forward, playing_, now);
}

MediaError PcMediaAdapter::previous(model::Millis now) {
    if (!linkUp_) return MediaError::NotAvailable;
    return control(pc::Control::Rewind, playing_, now);
}

MediaError PcMediaAdapter::seekAbsolute(int percent, model::Millis now) {
    (void)now;
    if (!linkUp_) return MediaError::NotAvailable;
    if (!v2Seen_) return MediaError::NotSupported;
    char body[pc::kBodyCapacity];
    if (pc::encodeSeekAbsolute(body, sizeof(body), percent) < 0) return MediaError::InvalidArgument;
    return publish(pc::kTopicControl, body) ? MediaError::Ok : MediaError::Transport;
}

MediaError PcMediaAdapter::seekRelative(int32_t deltaMs, model::Millis now) {
    (void)now;
    if (!linkUp_) return MediaError::NotAvailable;
    if (!v2Seen_) return MediaError::NotSupported;
    char body[pc::kBodyCapacity];
    if (pc::encodeSeekRelative(body, sizeof(body), deltaMs) < 0) return MediaError::InvalidArgument;
    return publish(pc::kTopicControl, body) ? MediaError::Ok : MediaError::Transport;
}

MediaError PcMediaAdapter::requestState(model::Millis now) {
    (void)now;
    if (!linkUp_) return MediaError::NotAvailable;
    char body[pc::kBodyCapacity];
    // The v1 word: the current PC app answers nothing else (is_state_request), and a v2 PC answers
    // it with the full tuple, so v2 is learned from the reply and never negotiated.
    pc::encodeStateRequest(body, sizeof(body));
    return publish(pc::kTopicGetState, body) ? MediaError::Ok : MediaError::Transport;
}

// ---- inbound (MEDIA-05, MEDIA-10) ----

bool PcMediaAdapter::onResponse(const char* payload, model::Millis now) {
    pc::Response r;
    if (!pc::parseResponse(payload, r)) {
        return false;
    }
    const uint32_t at = static_cast<uint32_t>(now);
    if (r.hasPlayState) {
        playing_ = r.playing;
        controlSentMs_ = -1;
        post(MediaModel::make(MediaModel::Field::IsPlaying, r.playing ? 1 : 0, at));
    }
    if (r.hasVolume && acceptRemoteVolume(r.volume, now)) {
        post(MediaModel::make(MediaModel::Field::Volume, r.volume, at));
    }
    if (r.hasPosition || r.hasDuration) {
        // A v2 reply carries both; with one missing, keep what the model knows for the other.
        int32_t position = r.positionMs;
        int32_t duration = r.durationMs;
        if (media_ != nullptr) {
            if (!r.hasPosition) position = media_->state().positionMs;
            if (!r.hasDuration) duration = media_->state().durationMs;
        }
        post(MediaModel::make(MediaModel::Field::Position, position, at, duration));
    }
    if (r.v2 && !v2Seen_) {
        v2Seen_ = true;
        post(MediaModel::make(MediaModel::Field::SeekResolution, static_cast<int32_t>(model::SeekResolution::Coarse), at));
    }
    if (media_ != nullptr) {
        if (r.hasPlayer) {
            player_.assign(r.player.c_str());
            media_->setTargetLabel(r.player.c_str());
        }
        if (r.hasTitle) {
            media_->setTrack(r.title.c_str(), "", "", "");
        }
    }
    if (!synced_) {
        synced_ = true;
        post(MediaModel::make(MediaModel::Field::StateSynced, 1, at));
        post(MediaModel::make(MediaModel::Field::Availability, static_cast<int32_t>(MediaTargetId::Pc), at,
                              static_cast<int32_t>(MediaAvailability::Ok)));
    }
    post(MediaModel::make(MediaModel::Field::StateSeen, 0, at));
    return true;
}

}  // namespace media
