#pragma once
// NavigationModel (MDL-26): the active screen, the wheel's selected mode, one activity clock,
// boot progress, and a navigation request that is consumed exactly once. Three inactivity
// clocks and a flag-per-screen exist today; this replaces them (D7, D8).
#include <cstdint>

#include "model/AppScreen.h"
#include "model/ModelBase.h"
#include "model/ModelTypes.h"
#include "model/MutationQueue.h"

namespace model {

enum class TransitionType : uint8_t { Immediate = 0, Animated = 1 };

struct NavigationRequest {
    AppScreen screen = AppScreen::Count;
    TransitionType transition = TransitionType::Immediate;
    bool valid() const { return screen < AppScreen::Count; }
};

struct NavigationState {
    AppScreen activeScreen = AppScreen::Boot;
    KnobMode selectedMode = KnobMode::Media;   // the wheel's default entry
    Millis lastActivityMs = 0;
    uint8_t initProgressPct = 0;               // the only home for boot progress
    bool requestPending = false;
    AppScreen requestedScreen = AppScreen::Count;
    TransitionType requestedTransition = TransitionType::Immediate;
    uint32_t requestsConsumed = 0;

    bool operator==(const NavigationState& other) const {
        return activeScreen == other.activeScreen && selectedMode == other.selectedMode &&
               lastActivityMs == other.lastActivityMs && initProgressPct == other.initProgressPct &&
               requestPending == other.requestPending && requestedScreen == other.requestedScreen &&
               requestedTransition == other.requestedTransition && requestsConsumed == other.requestsConsumed;
    }
    bool operator!=(const NavigationState& other) const { return !(*this == other); }
};

class NavigationModel : public ModelBase<NavigationState, 8> {
public:
    enum class Field : uint8_t {
        Request = 0,        // value = AppScreen, aux = TransitionType (never-drop)
        Activity = 1,       // no operand; stamps lastActivityMs with the drain time (coalesce)
        InitProgress = 2,   // value = percent (coalesce)
        Mode = 3,           // value = KnobMode (never-drop)
        ModeStep = 4,       // value = signed steps around the wheel (never-drop)
        ActiveScreen = 5,   // value = AppScreen: the router confirms a transition (never-drop)
        Count = 6
    };

    static RecordClass classOf(Field field);
    static ModelUpdate make(Field field, int32_t value, uint32_t atMs, int32_t aux = 0);
    bool applyUpdate(const ModelUpdate& update, Millis now);

    // The transition table. Every screen has at least one entry; SLEEP has none because it is
    // not a screen (D8).
    static bool canTransition(AppScreen from, AppScreen to);

    // A request is an intent for the router. A later request before consumption replaces the
    // earlier one; the never-drop class guarantees the record arrives, this guarantees only the
    // latest intent is acted on.
    bool requestScreen(AppScreen screen, TransitionType transition = TransitionType::Immediate);
    bool hasRequest() const { return state().requestPending; }
    // Returns the pending request and clears it. A second call returns an invalid request.
    NavigationRequest takeRequest();

    bool setActiveScreen(AppScreen screen);  // the router confirms; Count is rejected
    void noteActivity(Millis now);           // the one activity clock
    Millis idleMs(Millis now) const;

    bool setInitProgress(int percent);       // clamped 0..100, never moves backwards
    void resetInitProgress();

    bool selectMode(KnobMode mode);
    KnobMode stepMode(int steps);            // wraps around the wheel
    static AppScreen screenFor(KnobMode mode);
};

}  // namespace model
