#pragma once
// IMediaTarget (MEDIA-02): what a media target can do for the MEDIA screen, target-agnostic. The
// PC app over MQTT and Spotify over HTTPS sit behind it; the registry hands out a null target for
// anything unregistered, so a caller never tests a pointer. Every call takes the caller clock and
// returns a MediaError instead of blocking; the target reports back into the models through
// records on its own time. Pure: no ESP include, no heap.
#include <cstdint>

#include "model/MediaModel.h"
#include "model/ModelTypes.h"

namespace media {

using model::MediaTargetId;

enum class MediaError : uint8_t {
    Ok = 0,
    NotAvailable,     // no target, link down, or no state seen yet
    NotSupported,     // the target cannot do this (seek on a v1 PC, for example)
    Busy,             // a request of this kind is already in flight
    InvalidArgument,  // out of range
    Transport         // the publish itself failed
};
const char* mediaErrorName(MediaError error);

struct MediaCaps {
    bool volume = false;
    bool playPause = false;
    bool toggle = false;
    bool skip = false;  // next() / previous()
    bool seekAbsolute = false;
    bool seekRelative = false;
    bool trackInfo = false;
    bool position = false;
    uint8_t seekStepPercent = 0;  // 0 while seek is unavailable, else 10 (coarse) or 1 (fine)
};

class IMediaTarget {
public:
    virtual ~IMediaTarget() = default;
    virtual MediaTargetId id() const = 0;
    virtual const char* label() const = 0;
    virtual MediaCaps caps() const = 0;
    virtual bool available() const = 0;
    virtual MediaError setVolume(int percent, model::Millis now) = 0;
    virtual MediaError play(model::Millis now) = 0;
    virtual MediaError pause(model::Millis now) = 0;
    virtual MediaError toggle(model::Millis now) = 0;
    virtual MediaError next(model::Millis now) = 0;
    virtual MediaError previous(model::Millis now) = 0;
    virtual MediaError seekAbsolute(int percent, model::Millis now) = 0;
    virtual MediaError seekRelative(int32_t deltaMs, model::Millis now) = 0;
    virtual MediaError requestState(model::Millis now) = 0;
    // Once per frame: timers, coalesced publishes, staleness.
    virtual void tick(model::Millis now) = 0;
};

}  // namespace media
