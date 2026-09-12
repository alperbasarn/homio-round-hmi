#pragma once
// Stand-in for an IMediaTarget adapter, for the knob-binding machine tests (MEDIA-15). Records
// every call in order. Allocates, so use it outside NoHeapScope.
#include <string>
#include <vector>

namespace fakes {

struct FakeMediaTarget {
    bool available = true;
    int volume = -1;
    int seekPercent = -1;
    bool playing = false;
    std::vector<std::string> calls;

    void setVolume(int percent) {
        volume = percent;
        calls.push_back("setVolume(" + std::to_string(percent) + ")");
    }
    void play() { playing = true; calls.push_back("play"); }
    void pause() { playing = false; calls.push_back("pause"); }
    void togglePlayPause() { playing = !playing; calls.push_back("togglePlayPause"); }
    void next() { calls.push_back("next"); }
    void previous() { calls.push_back("previous"); }
    void seekTo(int percent) {
        seekPercent = percent;
        calls.push_back("seekTo(" + std::to_string(percent) + ")");
    }
    bool isAvailable() const { return available; }
};

}  // namespace fakes
