#pragma once
// NullMediaTarget (MEDIA-03): the target the registry answers with for an unregistered slot.
// Everything is NotAvailable, nothing is supported, nothing crashes, nothing allocates.
#include "media_target/IMediaTarget.h"

namespace media {

class NullMediaTarget final : public IMediaTarget {
public:
    explicit NullMediaTarget(MediaTargetId id = MediaTargetId::Count) : id_(id) {}
    MediaTargetId id() const override { return id_; }
    const char* label() const override { return "none"; }
    MediaCaps caps() const override { return MediaCaps{}; }
    bool available() const override { return false; }
    MediaError setVolume(int, model::Millis) override { return MediaError::NotAvailable; }
    MediaError play(model::Millis) override { return MediaError::NotAvailable; }
    MediaError pause(model::Millis) override { return MediaError::NotAvailable; }
    MediaError toggle(model::Millis) override { return MediaError::NotAvailable; }
    MediaError seekAbsolute(int, model::Millis) override { return MediaError::NotAvailable; }
    MediaError seekRelative(int32_t, model::Millis) override { return MediaError::NotAvailable; }
    MediaError requestState(model::Millis) override { return MediaError::NotAvailable; }
    void tick(model::Millis) override {}

private:
    MediaTargetId id_;
};

}  // namespace media
