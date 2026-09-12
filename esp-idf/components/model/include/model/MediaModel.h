#pragma once
// MediaModel (MDL-14) with the design amendments (UIX-10).
//
// Replaces SoundController's state: the real volume (its `setpoint`), play state, the active
// target and what the knob drives. Two targets sit behind one interface (Spotify, PC); each has
// its own availability slot so "Spotify has no token" and "PC is offline" can both be true.
#include <cstdint>

#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"
#include "model/RateLimit.h"

namespace model {

enum class MediaTargetId : uint8_t { Spotify = 0, Pc = 1, Count = 2 };
enum class KnobBinding : uint8_t { Volume = 0, Seek = 1 };

// What absolute seek the active target can do. Coarse is the PC's digit-key path (10 % steps);
// Fine is the browser extension or the Spotify API (1 %).
enum class SeekResolution : uint8_t { None = 0, Coarse = 1, Fine = 2 };

enum class MediaAvailability : uint8_t {
    Ok = 0,
    // Spotify slot
    NoToken = 1,
    NoPremium = 2,
    Offline = 3,
    Syncing = 4,
    // PC slot
    PcOffline = 5,
    NoPlayer = 6,
    NoMqtt = 7,
    Stale = 8,
    Count = 9
};

struct TrackInfo {
    FixedString<64> title;
    FixedString<64> artist;
    FixedString<64> album;
    FixedString<24> id;  // Spotify ids are 22 characters; the PC has none

    bool operator==(const TrackInfo& other) const {
        return title == other.title && artist == other.artist && album == other.album && id == other.id;
    }
    bool operator!=(const TrackInfo& other) const { return !(*this == other); }
};

struct MediaState {
    MediaTargetId activeTarget = MediaTargetId::Pc;  // the only target that exists today
    Percent volume = 50;                              // SoundController's default
    bool isPlaying = false;
    bool stateSynced = false;                         // a state reply arrived for the active target
    KnobBinding knobBinding = KnobBinding::Volume;
    SeekResolution seekResolution = SeekResolution::None;
    uint8_t seekStepPercent = 0;                      // 0 while seek is unavailable, else 10 or 1
    int8_t pendingSeekPercent = -1;                   // -1 none; 0..100 waiting for one seekTo()
    MediaAvailability availability[2] = {MediaAvailability::NoToken, MediaAvailability::PcOffline};
    int32_t positionMs = 0;
    int32_t durationMs = 0;                           // 0: unknown, which blocks SEEK
    Millis positionSampledAtMs = -1;                  // -1: never sampled
    Millis lastStateMs = -1;                          // -1: never; staleness is measured from it
    TrackInfo track;
    FixedString<24> targetLabel;                      // PC: the player the app detected

    bool operator==(const MediaState& other) const;
    bool operator!=(const MediaState& other) const { return !(*this == other); }
};

class MediaModel : public ModelBase<MediaState, 8> {
public:
    enum class Field : uint8_t {
        Volume = 0,         // value = percent (coalesce)
        IsPlaying = 1,      // value = bool (never-drop)
        ActiveTarget = 2,   // value = MediaTargetId (never-drop)
        KnobBinding = 3,    // value = KnobBinding (never-drop)
        Position = 4,       // value = positionMs, aux = durationMs (coalesce)
        Availability = 5,   // value = MediaTargetId, aux = MediaAvailability (never-drop)
        StateSynced = 6,    // value = bool (never-drop)
        PendingSeek = 7,    // value = percent or -1 (coalesce)
        SeekResolution = 8, // value = SeekResolution (never-drop)
        StateSeen = 9,      // no operand; stamps lastStateMs with the drain time (coalesce)
        Count = 10
    };

    static constexpr Millis kStateStaleMs = 10000;   // a state older than this is stale
    static constexpr Millis kEchoWindowMs = 1500;    // our own published volume echoed back
    static constexpr Millis kPublishIntervalMs = 25; // the CTRL-04 slot
    static constexpr int kNoSeek = -1;

    MediaModel();

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs, int32_t aux = 0);
    // False when the field is unknown or the value was rejected.
    bool applyUpdate(const ModelUpdate& update, Millis now);

    // Volume 0..100. Anything else is rejected, including the -1 an empty payload decodes to:
    // an empty payload used to mute the device.
    bool setVolume(int percent);
    // A volume reported by the target. Rejected when it is the echo of our own recent publish.
    bool setVolumeFromRemote(int percent, Millis now);
    bool isVolumeEcho(int percent, Millis now) const;
    // Publish bookkeeping: mark on every local change, ask once per frame, flush on leave.
    void markVolumeChanged();
    bool shouldPublishVolume(Millis now);
    bool flushVolumePublish(Millis now);
    bool volumePublishPending() const { return volumePublish_.pending(); }

    bool setPlaying(bool playing, Millis now);  // also counts as a state sighting
    bool setStateSynced(bool synced);
    void noteStateSeen(Millis now);
    bool isStateStale(Millis now) const;

    // Switching target clears everything learned about the old one.
    bool setActiveTarget(MediaTargetId target);
    bool setKnobBinding(KnobBinding binding);

    // Strings truncate to their FixedString capacity. Returns true when anything changed.
    bool setTrack(const char* title, const char* artist, const char* album, const char* id);
    bool setTargetLabel(const char* label);

    // positionMs clamps to durationMs; negative operands are rejected.
    bool setPosition(int32_t positionMs, int32_t durationMs, Millis now);
    // Extrapolates from the last sample while playing; -1 when never sampled.
    int32_t estimatedPositionMs(Millis now) const;

    bool setAvailability(MediaTargetId target, MediaAvailability state);
    static bool availabilityValidFor(MediaTargetId target, MediaAvailability state);
    MediaAvailability availabilityOf(MediaTargetId target) const;
    bool activeTargetAvailable() const;

    bool setPendingSeekPercent(int percent);  // kNoSeek clears; 0 is a real pending seek
    bool hasPendingSeek() const { return state().pendingSeekPercent >= 0; }
    int takePendingSeek();                    // returns and clears; kNoSeek when none
    bool setSeekResolution(SeekResolution resolution);
    bool canSeek() const;

private:
    RateLimiter volumePublish_;
    EchoSuppressor volumeEcho_;
};

}  // namespace model
