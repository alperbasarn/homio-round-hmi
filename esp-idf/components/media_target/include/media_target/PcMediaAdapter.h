#pragma once
// PcMediaAdapter (MEDIA-04, MEDIA-07, MEDIA-08, MEDIA-09, MEDIA-10): the PC media app as an
// IMediaTarget. Publishes through an injected function (the composition binds the MQTT manager,
// a test binds a recorder), so the adapter is pure and heap-free.
//
//   Volume    coalesced to one publish per 120 ms window, the last value never lost, plus a
//             settling retry 200 ms after the knob stops; a generation counter marks the echo
//             the PC sends back so it never lands in the model as a remote change.
//   Play      the PC maps play and pause onto one media key (VK_MEDIA_PLAY_PAUSE), so play
//             while playing would pause: both verbs are gated on the last known state, and
//             toggle is suppressed for 600 ms after any control until the reply lands.
//   Seek      default closed: seek_abs / seek_rel publish only after a v2 reply was seen.
//   State     request:v2 on every request; an old PC answers v1 and position stays unknown.
//
// Inbound responses are handed in on the display task (MEDIA-06 rings them over from the MQTT
// task) and land in the MediaModel as records; strings go through the model pointer.
#include <cstdint>

#include "media_target/IMediaTarget.h"
#include "media_target/PcProtocol.h"
#include "model/MediaModel.h"
#include "model/MutationQueue.h"
#include "model/RateLimit.h"

namespace media {

using PublishFn = bool (*)(const char* topic, const char* body, void* context);

class PcMediaAdapter final : public IMediaTarget {
public:
    static constexpr model::Millis kVolumeWindowMs = 120;
    static constexpr model::Millis kVolumeSettleMs = 200;
    static constexpr model::Millis kEchoWindowMs = 1500;
    static constexpr model::Millis kControlSuppressMs = 600;

    // media may be null (records only): a reply's strings then have nowhere to go.
    PcMediaAdapter(model::MutationQueue* queue, model::MediaModel* media, PublishFn publish, void* context);

    // The MQTT connection, from the composition. Coming up asks the PC for its state.
    void setLinkUp(bool up, model::Millis now);
    // Display task: a body from esp32/sound/response (the sender envelope may still be on it).
    bool onResponse(const char* payload, model::Millis now);

    MediaTargetId id() const override { return MediaTargetId::Pc; }
    const char* label() const override { return player_.empty() ? "PC" : player_.c_str(); }
    MediaCaps caps() const override;
    bool available() const override { return linkUp_ && synced_; }
    MediaError setVolume(int percent, model::Millis now) override;
    MediaError play(model::Millis now) override;
    MediaError pause(model::Millis now) override;
    MediaError toggle(model::Millis now) override;
    MediaError seekAbsolute(int percent, model::Millis now) override;
    MediaError seekRelative(int32_t deltaMs, model::Millis now) override;
    MediaError requestState(model::Millis now) override;
    void tick(model::Millis now) override;

    bool linkUp() const { return linkUp_; }
    bool v2Seen() const { return v2Seen_; }
    bool playing() const { return playing_; }
    bool volumePending() const { return pendingVolume_ >= 0; }
    bool volumeInFlight() const { return sentGeneration_ != ackedGeneration_; }
    uint32_t publishes() const { return publishes_; }
    uint32_t publishFailures() const { return publishFailures_; }
    uint32_t echoesDropped() const { return echoesDropped_; }

private:
    bool publish(const char* topic, const char* body);
    void post(const model::ModelUpdate& record);
    MediaError control(pc::Control verb, bool playingAfter, model::Millis now);
    void flushVolume(model::Millis now);
    bool acceptRemoteVolume(int volume, model::Millis now);

    model::MutationQueue* queue_;
    model::MediaModel* media_;
    PublishFn publish_;
    void* context_;
    bool linkUp_ = false;
    bool synced_ = false;
    bool v2Seen_ = false;
    bool playing_ = false;
    int pendingVolume_ = -1;
    int lastSentVolume_ = -1;
    uint32_t sentGeneration_ = 0;
    uint32_t ackedGeneration_ = 0;
    model::Millis lastVolumeSentMs_ = 0;
    model::Millis controlSentMs_ = -1;  // -1: none in flight
    model::RateLimiter volumeLimiter_;
    model::Debouncer settle_;
    model::FixedString<24> player_;
    uint32_t publishes_ = 0;
    uint32_t publishFailures_ = 0;
    uint32_t echoesDropped_ = 0;
};

}  // namespace media
