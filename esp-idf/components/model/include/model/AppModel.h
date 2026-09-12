#pragma once
#include <cstddef>
#include <cstdint>

#include "model/Clock.h"
#include "model/MutationQueue.h"

namespace model {

// Aggregate root (MDL-30). Held by value in the composition.
//
// WP1.1 ships the mutation path and the one-shot wiring guard. WP1.3 adds the eight models as
// members and fills apply() so each ModelUpdate reaches its aggregate; until then every drained
// record is counted as unrouted so the shadow-population tasks can see what they post.
class AppModel {
public:
    AppModel();

    MutationQueue& mutations() { return mutations_; }
    const MutationQueue& mutations() const { return mutations_; }

    void setClock(const Clock& clock) { clock_ = clock; }
    Millis nowMs() const { return clock_.nowMs(); }

    // Subscribes every controller and adapter once, at startup. A second call returns false and
    // changes nothing: subscriptions are fixed-capacity and the render path must not re-wire.
    bool wireSubscriptions();
    bool wired() const { return wired_; }

    // Drains the mutation queue into the models. Called once at the top of the display frame.
    size_t drainPendingMutations(Millis nowMs);

    uint32_t unroutedRecords() const { return unrouted_; }

private:
    void apply(const ModelUpdate& record, Millis nowMs);

    MutationQueue mutations_;
    Clock clock_;
    bool wired_;
    uint32_t unrouted_;
};

}  // namespace model
