#pragma once
// MediaScreenState (MEDIA-13 / CTRL-15): what the MEDIA view renders. The controller fills it once
// per frame from the media model and its own phase; the view owns nothing but LVGL handles.
#include <cstdint>

#include "model/MediaModel.h"

namespace ui {

enum class MediaPhase : uint8_t {
    WaitForSync = 0,  // the state request is out; the ring shows the last known volume dimmed
    Live,             // a state reply arrived for the active target
    Unsynced          // no reply within the sync window: local controls work, the target may not
};

struct MediaScreenState {
    const model::MediaState* media = nullptr;
    MediaPhase phase = MediaPhase::WaitForSync;
    bool targetAvailable = false;
    const char* targetLabel = "";
    int scrubPercent = -1;        // the seek position being dialled, -1 while not scrubbing
    bool unavailableFlash = false; // a tap or a skip the target refused, shown for a moment
};

}  // namespace ui
