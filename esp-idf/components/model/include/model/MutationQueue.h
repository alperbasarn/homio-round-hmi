#pragma once
#include <cstddef>
#include <cstdint>

namespace model {

// Which model a record targets. Field ids are per aggregate; each model defines its own enum.
enum class Aggregate : uint8_t {
    Media,
    Light,
    Climate,
    Environment,
    Connectivity,
    Power,
    Navigation,
    Knob,
    Count
};

// D18 record classes.
enum class RecordClass : uint8_t {
    CoalesceLatest = 0,  // a newer record for the same (aggregate, field) overwrites the pending one in place
    NeverDrop = 1,       // must be delivered; at capacity the producer retries, then faults
};

// The unit of cross-task mutation (D1). Plain data, 16 bytes, no pointers, no strings. Producers
// on any task post these; the display task drains them at the top of its frame and is the only
// writer of the models themselves.
struct ModelUpdate {
    uint8_t aggregate;  // Aggregate
    uint8_t field;      // per-aggregate field id
    uint8_t cls;        // RecordClass
    uint8_t flags;      // bit 0: value holds float bits
    int32_t value;      // int, bool, enum, or float bits
    int32_t aux;        // second operand where a field needs one (durationMs beside positionMs, ...)
    uint32_t atMs;      // low 32 bits of the producer's clock

    static constexpr uint8_t kFlagFloat = 0x01u;

    static ModelUpdate ofInt(Aggregate aggregate, uint8_t field, RecordClass cls, int32_t value, uint32_t atMs, int32_t aux = 0);
    static ModelUpdate ofBool(Aggregate aggregate, uint8_t field, RecordClass cls, bool value, uint32_t atMs);
    static ModelUpdate ofFloat(Aggregate aggregate, uint8_t field, RecordClass cls, float value, uint32_t atMs);

    bool isFloat() const { return (flags & kFlagFloat) != 0; }
    float asFloat() const;
    bool asBool() const { return value != 0; }
};
static_assert(sizeof(ModelUpdate) == 16, "ModelUpdate must stay a 16-byte POD (D1 budget: 64 slots)");

// Fixed-capacity multi-producer / single-consumer queue (D1) with the D18 overflow policy.
//
// Every hook is injectable so the class has no platform includes:
//   lock/unlock - the composition installs a critical section on the device and a mutex on the
//                 host. Without hooks the queue is only safe from one thread.
//   sleep       - used between never-drop retries (3 tries, 2 ms apart); null means no waiting.
//   log         - receives the WARN lines; null means silent.
//   drainGuard  - returns true when the caller is the consumer task; a failing guard is counted.
class MutationQueue {
public:
    static constexpr size_t kCapacity = 64;
    static constexpr int kNeverDropRetries = 3;
    static constexpr uint32_t kNeverDropRetryMs = 2;

    using LockFn = void (*)(void* context);
    using SleepFn = void (*)(uint32_t ms, void* context);
    using LogFn = void (*)(const char* line, void* context);
    using GuardFn = bool (*)(void* context);

    MutationQueue();

    void setLockHooks(LockFn lock, LockFn unlock, void* context);
    void setSleepHook(SleepFn sleep, void* context);
    void setLogHook(LogFn log, void* context);
    void setDrainGuard(GuardFn guard, void* context);

    // Producer side. A coalesce-latest record whose key is already pending overwrites it in place
    // and never takes a slot. Returns false only when a record could not be enqueued: for a
    // never-drop record that is after the retries (a fault), for a coalesce-latest record it is a
    // plain overflow. Both bump overflowCount().
    bool post(const ModelUpdate& record);

    // Consumer side: delivers pending records in FIFO order to apply(const ModelUpdate&) and
    // returns how many were delivered. Must run on the consumer task (see setDrainGuard).
    template <typename Apply>
    size_t drain(Apply&& apply) {
        checkDrainTask();
        size_t delivered = 0;
        ModelUpdate record;
        while (pop(record)) {
            apply(static_cast<const ModelUpdate&>(record));
            ++delivered;
        }
        return delivered;
    }

    size_t size() const;
    bool empty() const { return size() == 0; }
    uint32_t overflowCount() const { return overflows_; }
    uint32_t neverDropFaults() const { return neverDropFaults_; }
    uint32_t coalescedCount() const { return coalesced_; }
    uint32_t offTaskDrains() const { return offTaskDrains_; }
    bool lockHooksInstalled() const { return lock_ != nullptr && unlock_ != nullptr; }

private:
    bool tryEnqueueLocked(const ModelUpdate& record);  // caller holds the lock
    bool pop(ModelUpdate& out);
    void checkDrainTask();
    void lock();
    void unlock();
    void logLine(const char* line);

    ModelUpdate slots_[kCapacity];
    size_t head_;   // next slot to pop
    size_t count_;  // pending records
    uint32_t overflows_;
    uint32_t neverDropFaults_;
    uint32_t coalesced_;
    uint32_t offTaskDrains_;

    LockFn lock_;
    LockFn unlock_;
    void* lockContext_;
    SleepFn sleep_;
    void* sleepContext_;
    LogFn log_;
    void* logContext_;
    GuardFn guard_;
    void* guardContext_;
};

}  // namespace model
