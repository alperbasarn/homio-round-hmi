#include "model/MutationQueue.h"

#include <cstring>

namespace model {

ModelUpdate ModelUpdate::ofInt(Aggregate aggregate, uint8_t field, RecordClass cls, int32_t value, uint32_t atMs, int32_t aux) {
    ModelUpdate record{};
    record.aggregate = static_cast<uint8_t>(aggregate);
    record.field = field;
    record.cls = static_cast<uint8_t>(cls);
    record.flags = 0;
    record.value = value;
    record.aux = aux;
    record.atMs = atMs;
    return record;
}

ModelUpdate ModelUpdate::ofBool(Aggregate aggregate, uint8_t field, RecordClass cls, bool value, uint32_t atMs) {
    return ofInt(aggregate, field, cls, value ? 1 : 0, atMs);
}

ModelUpdate ModelUpdate::ofFloat(Aggregate aggregate, uint8_t field, RecordClass cls, float value, uint32_t atMs) {
    int32_t bits = 0;
    static_assert(sizeof(bits) == sizeof(value), "float must be 32-bit");
    std::memcpy(&bits, &value, sizeof(bits));
    ModelUpdate record = ofInt(aggregate, field, cls, bits, atMs);
    record.flags |= kFlagFloat;
    return record;
}

float ModelUpdate::asFloat() const {
    float result = 0.0f;
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

MutationQueue::MutationQueue()
    : slots_{},
      head_(0),
      count_(0),
      overflows_(0),
      neverDropFaults_(0),
      coalesced_(0),
      offTaskDrains_(0),
      lock_(nullptr),
      unlock_(nullptr),
      lockContext_(nullptr),
      sleep_(nullptr),
      sleepContext_(nullptr),
      log_(nullptr),
      logContext_(nullptr),
      guard_(nullptr),
      guardContext_(nullptr) {}

void MutationQueue::setLockHooks(LockFn lock, LockFn unlock, void* context) {
    lock_ = lock;
    unlock_ = unlock;
    lockContext_ = context;
}

void MutationQueue::setSleepHook(SleepFn sleep, void* context) {
    sleep_ = sleep;
    sleepContext_ = context;
}

void MutationQueue::setLogHook(LogFn log, void* context) {
    log_ = log;
    logContext_ = context;
}

void MutationQueue::setDrainGuard(GuardFn guard, void* context) {
    guard_ = guard;
    guardContext_ = context;
}

void MutationQueue::lock() {
    if (lock_ != nullptr) {
        lock_(lockContext_);
    }
}

void MutationQueue::unlock() {
    if (unlock_ != nullptr) {
        unlock_(lockContext_);
    }
}

void MutationQueue::logLine(const char* line) {
    if (log_ != nullptr) {
        log_(line, logContext_);
    }
}

size_t MutationQueue::size() const {
    return count_;
}

bool MutationQueue::tryEnqueueLocked(const ModelUpdate& record) {
    if (record.cls == static_cast<uint8_t>(RecordClass::CoalesceLatest)) {
        // D18: the pending record for the same key is overwritten in place, keeping its FIFO
        // position, so a 100-step knob sweep or an RSSI storm occupies one slot.
        for (size_t i = 0; i < count_; ++i) {
            ModelUpdate& pending = slots_[(head_ + i) % kCapacity];
            if (pending.aggregate == record.aggregate && pending.field == record.field &&
                pending.cls == record.cls) {
                pending.value = record.value;
                pending.aux = record.aux;
                pending.flags = record.flags;
                pending.atMs = record.atMs;
                ++coalesced_;
                return true;
            }
        }
    }
    if (count_ >= kCapacity) {
        return false;
    }
    slots_[(head_ + count_) % kCapacity] = record;
    ++count_;
    return true;
}

bool MutationQueue::post(const ModelUpdate& record) {
    const bool neverDrop = record.cls == static_cast<uint8_t>(RecordClass::NeverDrop);
    for (int attempt = 0;; ++attempt) {
        lock();
        if (tryEnqueueLocked(record)) {
            unlock();
            return true;
        }
        if (neverDrop && attempt < kNeverDropRetries) {
            unlock();
            if (sleep_ != nullptr) {
                sleep_(kNeverDropRetryMs, sleepContext_);
            }
            continue;
        }
        ++overflows_;
        if (neverDrop) {
            ++neverDropFaults_;
        }
        unlock();
        logLine(neverDrop ? "MUTQ overflow never-drop cls=1" : "MUTQ overflow coalesce-latest cls=0");
        return false;
    }
}

bool MutationQueue::pop(ModelUpdate& out) {
    lock();
    if (count_ == 0) {
        unlock();
        return false;
    }
    out = slots_[head_];
    head_ = (head_ + 1) % kCapacity;
    --count_;
    unlock();
    return true;
}

void MutationQueue::checkDrainTask() {
    if (guard_ != nullptr && !guard_(guardContext_)) {
        ++offTaskDrains_;
        logLine("MUTQ drain called off the consumer task");
    }
}

}  // namespace model
