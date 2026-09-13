#include "media_target/MediaTargetRegistry.h"

namespace media {

const char* mediaErrorName(MediaError error) {
    switch (error) {
        case MediaError::Ok: return "ok";
        case MediaError::NotAvailable: return "not available";
        case MediaError::NotSupported: return "not supported";
        case MediaError::Busy: return "busy";
        case MediaError::InvalidArgument: return "invalid argument";
        case MediaError::Transport: return "transport";
        default: return "?";
    }
}

MediaTargetRegistry::MediaTargetRegistry()
    : slots_{nullptr, nullptr}, nulls_{NullMediaTarget(MediaTargetId::Spotify), NullMediaTarget(MediaTargetId::Pc)}, nullAny_() {}

bool MediaTargetRegistry::registerTarget(IMediaTarget* target) {
    if (target == nullptr || !validId(target->id())) {
        return false;
    }
    IMediaTarget*& slot = slots_[static_cast<size_t>(target->id())];
    if (slot != nullptr) {
        return false;
    }
    slot = target;
    return true;
}

bool MediaTargetRegistry::unregisterTarget(MediaTargetId id) {
    if (!validId(id) || slots_[static_cast<size_t>(id)] == nullptr) {
        return false;
    }
    slots_[static_cast<size_t>(id)] = nullptr;
    return true;
}

bool MediaTargetRegistry::registered(MediaTargetId id) const {
    return validId(id) && slots_[static_cast<size_t>(id)] != nullptr;
}

IMediaTarget& MediaTargetRegistry::target(MediaTargetId id) {
    if (!validId(id)) {
        return nullAny_;
    }
    IMediaTarget* slot = slots_[static_cast<size_t>(id)];
    return slot != nullptr ? *slot : nulls_[static_cast<size_t>(id)];
}

const IMediaTarget& MediaTargetRegistry::target(MediaTargetId id) const {
    if (!validId(id)) {
        return nullAny_;
    }
    const IMediaTarget* slot = slots_[static_cast<size_t>(id)];
    return slot != nullptr ? *slot : nulls_[static_cast<size_t>(id)];
}

void MediaTargetRegistry::tickAll(model::Millis now) {
    for (IMediaTarget* slot : slots_) {
        if (slot != nullptr) {
            slot->tick(now);
        }
    }
}

}  // namespace media
