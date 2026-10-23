#pragma once
// MediaTargetRegistry (MEDIA-03): two fixed slots (Spotify, PC) and a null target for each, so
// target(id) never returns a null pointer and an unregistered target answers NotAvailable.
#include <cstddef>

#include "media_target/IMediaTarget.h"
#include "media_target/NullMediaTarget.h"

namespace media {

class MediaTargetRegistry {
public:
    static constexpr size_t kSlots = static_cast<size_t>(MediaTargetId::Count);

    MediaTargetRegistry();
    // False for a null pointer, an id outside the slots, or a slot already taken.
    bool registerTarget(IMediaTarget* target);
    bool unregisterTarget(MediaTargetId id);
    bool registered(MediaTargetId id) const;
    IMediaTarget& target(MediaTargetId id);
    const IMediaTarget& target(MediaTargetId id) const;
    void tickAll(model::Millis now);

private:
    static bool validId(MediaTargetId id) { return static_cast<size_t>(id) < kSlots; }
    IMediaTarget* slots_[kSlots];
    NullMediaTarget nulls_[kSlots];
    NullMediaTarget nullAny_;
};

}  // namespace media
