#pragma once
#include <cstddef>
#include <cstdint>

#include "model/ClimateModel.h"
#include "model/Clock.h"
#include "model/ConnectivityModel.h"
#include "model/EnvironmentModel.h"
#include "model/KnobModel.h"
#include "model/LightModel.h"
#include "model/MediaModel.h"
#include "model/MutationQueue.h"
#include "model/NavigationModel.h"
#include "model/PowerModel.h"

namespace model {

// Aggregate root (MDL-30). Held by value in the composition; the eight models are members.
//
// One frame: producers on any task post ModelUpdate records; the display task calls
// drainPendingMutations() at the top of its frame, which routes every record to its model,
// ticks the time-driven policies, then commits every model once so subscribers see each
// changed model exactly once per frame. Direct setter calls on the display task (the shadow
// phase, MDL-31) are committed by the same drain.
class AppModel {
public:
    using WireFn = void (*)(AppModel& app, void* context);

    AppModel();

    MutationQueue& mutations() { return mutations_; }
    const MutationQueue& mutations() const { return mutations_; }

    void setClock(const Clock& clock) { clock_ = clock; }
    Millis nowMs() const { return clock_.nowMs(); }

    // Runs the composition's wiring exactly once, then seals every model: a subscribe after
    // this returns false. A second call returns false and runs nothing.
    bool wireSubscriptions(WireFn wire = nullptr, void* context = nullptr);
    bool wired() const { return wired_; }

    // Drains the queue into the models, ticks the policies, commits. Returns the number of
    // records drained.
    size_t drainPendingMutations(Millis nowMs);
    // Commits every dirty model; returns how many notified. drainPendingMutations() calls it.
    size_t commitAll();

    uint32_t unroutedRecords() const { return unrouted_; }
    uint32_t rejectedRecords() const { return rejected_; }
    size_t lastCommitCount() const { return lastCommitCount_; }

    MediaModel& media() { return media_; }
    LightModel& light() { return light_; }
    ClimateModel& climate() { return climate_; }
    EnvironmentModel& environment() { return environment_; }
    ConnectivityModel& connectivity() { return connectivity_; }
    PowerModel& power() { return power_; }
    NavigationModel& navigation() { return navigation_; }
    KnobModel& knob() { return knob_; }

    const MediaModel& media() const { return media_; }
    const LightModel& light() const { return light_; }
    const ClimateModel& climate() const { return climate_; }
    const EnvironmentModel& environment() const { return environment_; }
    const ConnectivityModel& connectivity() const { return connectivity_; }
    const PowerModel& power() const { return power_; }
    const NavigationModel& navigation() const { return navigation_; }
    const KnobModel& knob() const { return knob_; }

private:
    void apply(const ModelUpdate& record, Millis nowMs);

    MutationQueue mutations_;
    Clock clock_;
    bool wired_;
    uint32_t unrouted_;
    uint32_t rejected_;
    size_t lastCommitCount_;

    MediaModel media_;
    LightModel light_;
    ClimateModel climate_;
    EnvironmentModel environment_;
    ConnectivityModel connectivity_;
    PowerModel power_;
    NavigationModel navigation_;
    KnobModel knob_;
};

}  // namespace model
