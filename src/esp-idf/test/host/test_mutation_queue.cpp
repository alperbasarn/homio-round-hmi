// test_mutation_queue (MDL-40): FIFO under capacity, coalesce-in-place, the D18 policy at
// capacity+1 for both classes, the overflow counter, the drain-task guard, and no allocation.
#include <cstring>
#include <string>
#include <vector>

#include "HeapGuard.h"
#include "TestRunner.h"
#include "model/MutationQueue.h"

using model::Aggregate;
using model::ModelUpdate;
using model::MutationQueue;
using model::RecordClass;

namespace {

constexpr uint8_t kVolume = 1;
constexpr uint8_t kIsPlaying = 2;
constexpr uint8_t kRssi = 3;

struct Hooks {
    int sleeps = 0;
    std::vector<std::string> lines;
    int guardCalls = 0;
    bool onConsumerTask = true;
};

void sleepHook(uint32_t, void* context) { ++static_cast<Hooks*>(context)->sleeps; }
void logHook(const char* line, void* context) { static_cast<Hooks*>(context)->lines.emplace_back(line); }
bool guardHook(void* context) {
    auto* hooks = static_cast<Hooks*>(context);
    ++hooks->guardCalls;
    return hooks->onConsumerTask;
}

ModelUpdate volumeRecord(int value, uint32_t at = 0) {
    return ModelUpdate::ofInt(Aggregate::Media, kVolume, RecordClass::CoalesceLatest, value, at);
}

ModelUpdate playingRecord(bool value, uint32_t at = 0) {
    return ModelUpdate::ofBool(Aggregate::Media, kIsPlaying, RecordClass::NeverDrop, value, at);
}

}  // namespace

TEST_CASE(record_is_sixteen_bytes) {
    CHECK_EQ(sizeof(ModelUpdate), static_cast<size_t>(16));
    CHECK_EQ(sizeof(ModelUpdate) * MutationQueue::kCapacity, static_cast<size_t>(1024));
}

TEST_CASE(float_records_round_trip) {
    const ModelUpdate record = ModelUpdate::ofFloat(Aggregate::Environment, 7, RecordClass::CoalesceLatest, 21.5f, 99);
    CHECK(record.isFloat());
    CHECK_EQ(record.asFloat(), 21.5f);
    CHECK_EQ(record.atMs, 99u);
    CHECK(!volumeRecord(3).isFloat());
}

TEST_CASE(fifo_order_under_capacity) {
    MutationQueue queue;
    for (int i = 0; i < 10; ++i) {
        CHECK(queue.post(ModelUpdate::ofInt(Aggregate::Knob, static_cast<uint8_t>(i), RecordClass::NeverDrop, i, 0)));
    }
    CHECK_EQ(queue.size(), static_cast<size_t>(10));
    int expected = 0;
    const size_t delivered = queue.drain([&](const ModelUpdate& r) {
        CHECK_EQ(r.value, expected);
        ++expected;
    });
    CHECK_EQ(delivered, static_cast<size_t>(10));
    CHECK(queue.empty());
}

TEST_CASE(coalesce_latest_overwrites_in_place) {
    MutationQueue queue;
    CHECK(queue.post(playingRecord(true, 1)));
    for (int v = 0; v < 100; ++v) {
        CHECK(queue.post(volumeRecord(v, static_cast<uint32_t>(10 + v))));
    }
    CHECK(queue.post(playingRecord(false, 200)));
    // 100 volume records collapsed into the one slot between the two never-drop records.
    CHECK_EQ(queue.size(), static_cast<size_t>(3));
    CHECK_EQ(queue.coalescedCount(), 99u);
    CHECK_EQ(queue.overflowCount(), 0u);

    int order = 0;
    queue.drain([&](const ModelUpdate& r) {
        if (order == 0) { CHECK_EQ(r.field, kIsPlaying); CHECK(r.asBool()); }
        if (order == 1) { CHECK_EQ(r.field, kVolume); CHECK_EQ(r.value, 99); CHECK_EQ(r.atMs, 109u); }
        if (order == 2) { CHECK_EQ(r.field, kIsPlaying); CHECK(!r.asBool()); }
        ++order;
    });
    CHECK_EQ(order, 3);
}

TEST_CASE(never_drop_at_capacity_retries_then_faults) {
    MutationQueue queue;
    Hooks hooks;
    queue.setSleepHook(&sleepHook, &hooks);
    queue.setLogHook(&logHook, &hooks);
    for (size_t i = 0; i < MutationQueue::kCapacity; ++i) {
        REQUIRE(queue.post(ModelUpdate::ofInt(Aggregate::Knob, static_cast<uint8_t>(i), RecordClass::NeverDrop, 1, 0)));
    }
    CHECK_EQ(queue.size(), MutationQueue::kCapacity);

    CHECK(!queue.post(playingRecord(true)));
    CHECK_EQ(hooks.sleeps, MutationQueue::kNeverDropRetries);
    CHECK_EQ(queue.neverDropFaults(), 1u);
    CHECK_EQ(queue.overflowCount(), 1u);
    REQUIRE(!hooks.lines.empty());
    CHECK(hooks.lines.back().find("MUTQ overflow never-drop") != std::string::npos);
}

TEST_CASE(coalesce_latest_at_capacity_with_a_new_key_overflows_without_retry) {
    MutationQueue queue;
    Hooks hooks;
    queue.setSleepHook(&sleepHook, &hooks);
    for (size_t i = 0; i < MutationQueue::kCapacity; ++i) {
        REQUIRE(queue.post(ModelUpdate::ofInt(Aggregate::Knob, static_cast<uint8_t>(i), RecordClass::NeverDrop, 1, 0)));
    }
    CHECK(!queue.post(ModelUpdate::ofInt(Aggregate::Connectivity, kRssi, RecordClass::CoalesceLatest, -60, 0)));
    CHECK_EQ(hooks.sleeps, 0);
    CHECK_EQ(queue.overflowCount(), 1u);
    CHECK_EQ(queue.neverDropFaults(), 0u);
}

TEST_CASE(coalesce_latest_at_capacity_with_a_pending_key_still_lands) {
    MutationQueue queue;
    REQUIRE(queue.post(volumeRecord(1)));
    for (size_t i = 1; i < MutationQueue::kCapacity; ++i) {
        REQUIRE(queue.post(ModelUpdate::ofInt(Aggregate::Knob, static_cast<uint8_t>(i), RecordClass::NeverDrop, 1, 0)));
    }
    CHECK_EQ(queue.size(), MutationQueue::kCapacity);
    CHECK(queue.post(volumeRecord(77)));  // overwrites the pending volume slot
    CHECK_EQ(queue.overflowCount(), 0u);
    bool sawVolume = false;
    queue.drain([&](const ModelUpdate& r) {
        if (r.field == kVolume && r.aggregate == static_cast<uint8_t>(Aggregate::Media)) {
            sawVolume = true;
            CHECK_EQ(r.value, 77);
        }
    });
    CHECK(sawVolume);
}

TEST_CASE(drain_off_the_consumer_task_is_counted) {
    MutationQueue queue;
    Hooks hooks;
    queue.setDrainGuard(&guardHook, &hooks);
    queue.setLogHook(&logHook, &hooks);
    queue.post(volumeRecord(5));
    queue.drain([](const ModelUpdate&) {});
    CHECK_EQ(queue.offTaskDrains(), 0u);

    hooks.onConsumerTask = false;
    queue.post(volumeRecord(6));
    queue.drain([](const ModelUpdate&) {});
    CHECK_EQ(queue.offTaskDrains(), 1u);
    CHECK_EQ(hooks.guardCalls, 2);
}

TEST_CASE(lock_hooks_bracket_every_operation) {
    struct LockLog { int locks = 0; int unlocks = 0; bool held = false; bool nested = false; } log;
    MutationQueue queue;
    queue.setLockHooks(
        [](void* ctx) { auto* l = static_cast<LockLog*>(ctx); if (l->held) l->nested = true; l->held = true; ++l->locks; },
        [](void* ctx) { auto* l = static_cast<LockLog*>(ctx); l->held = false; ++l->unlocks; },
        &log);
    CHECK(queue.lockHooksInstalled());
    queue.post(volumeRecord(1));
    queue.post(playingRecord(true));
    queue.drain([](const ModelUpdate&) {});
    CHECK_EQ(log.locks, log.unlocks);
    CHECK(!log.nested);
    CHECK(!log.held);
}

TEST_CASE(post_and_drain_allocate_nothing) {
    MutationQueue queue;
    int delivered = 0;
    {
        NO_HEAP_SCOPE();
        for (int i = 0; i < 1000; ++i) {
            queue.post(volumeRecord(i));
            queue.post(playingRecord((i & 1) != 0));
            if ((i % 8) == 7) {
                delivered += static_cast<int>(queue.drain([](const ModelUpdate&) {}));
            }
        }
        delivered += static_cast<int>(queue.drain([](const ModelUpdate&) {}));
    }
    CHECK(delivered > 0);
    CHECK(queue.empty());
}
