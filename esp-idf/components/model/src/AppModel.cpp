#include "model/AppModel.h"

namespace model {

AppModel::AppModel() : mutations_(), clock_(), wired_(false), unrouted_(0) {}

bool AppModel::wireSubscriptions() {
    if (wired_) {
        return false;
    }
    // WP1.3 / WP1.4: controllers and adapters subscribe to the models here, exactly once.
    wired_ = true;
    return true;
}

size_t AppModel::drainPendingMutations(Millis nowMs) {
    return mutations_.drain([this, nowMs](const ModelUpdate& record) { apply(record, nowMs); });
}

void AppModel::apply(const ModelUpdate& record, Millis nowMs) {
    (void)nowMs;
    switch (static_cast<Aggregate>(record.aggregate)) {
        case Aggregate::Media:
        case Aggregate::Light:
        case Aggregate::Climate:
        case Aggregate::Environment:
        case Aggregate::Connectivity:
        case Aggregate::Power:
        case Aggregate::Navigation:
        case Aggregate::Knob:
        case Aggregate::Count:
        default:
            // WP1.3 routes each aggregate to its model; nothing is routed yet.
            ++unrouted_;
            break;
    }
}

}  // namespace model
