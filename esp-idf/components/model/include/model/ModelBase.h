#pragma once
// The shape every model shares (WP1.3, MDL-30).
//
// A model is a plain-data State with defaults, mutated only on the display task through the
// derived class's setters (which call mutate()), and committed once per drain: commit() notifies
// subscribers when the state differs from the last notified copy and publishes the seqlock
// snapshot other tasks read (D2). Subscriptions are fixed-capacity function pointers (D3) and
// are sealed after AppModel::wireSubscriptions(), so nothing re-wires in the render path.
#include <cstddef>

#include "model/ModelSnapshot.h"
#include "model/Observable.h"

namespace model {

template <typename State, size_t MaxSubscribers>
class ModelBase {
public:
    using StateType = State;
    using Callback = typename Observable<State, MaxSubscribers>::Callback;
    static constexpr size_t kMaxSubscribers = MaxSubscribers;

    ModelBase() : state_(), dirty_(false), sealed_(false), observable_(), snapshot_() {
        snapshot_.publish(state_);
    }

    // The working copy. Display task only; other tasks use snapshot().
    const State& state() const { return state_; }

    // Returns false when sealed, full, or callback is null.
    bool subscribe(Callback callback, void* context) {
        if (sealed_) {
            return false;
        }
        return observable_.subscribe(callback, context);
    }
    size_t subscriberCount() const { return observable_.subscriberCount(); }
    void seal() { sealed_ = true; }
    bool sealed() const { return sealed_; }

    bool dirty() const { return dirty_; }

    // Notifies when the state changed since the last notification; an equal set notifies nobody.
    // Returns true when subscribers were notified (and the snapshot republished).
    bool commit() {
        if (!dirty_) {
            return false;
        }
        dirty_ = false;
        if (!observable_.set(state_)) {
            return false;
        }
        snapshot_.publish(state_);
        return true;
    }

    const ModelSnapshot<State>& snapshot() const { return snapshot_; }
    State readSnapshot() const { return snapshot_.read(); }

protected:
    State& mutate() {
        dirty_ = true;
        return state_;
    }

private:
    State state_;
    bool dirty_;
    bool sealed_;
    Observable<State, MaxSubscribers> observable_;
    ModelSnapshot<State> snapshot_;
};

}  // namespace model
