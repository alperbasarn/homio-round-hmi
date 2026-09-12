#pragma once
#include <atomic>
#include <cstdint>
#include <cstring>
#include <type_traits>

namespace model {

// Seqlock snapshot of a plain-data struct (NET-05, D2).
//
// The display task is the single writer of every model; adapters on other tasks that need a
// consistent view of one (the pollers, the MQTT bridge) read a snapshot instead of the model.
// publish() bumps the sequence to odd, copies, then bumps to even; read() copies and retries
// while the sequence is odd or changed underneath it. No blocking primitive, no allocation.
template <typename T>
class ModelSnapshot {
    static_assert(std::is_trivially_copyable<T>::value, "snapshots hold plain data only");

public:
    ModelSnapshot() : sequence_(0), data_() {}

    // Writer side (one writer).
    void publish(const T& value) {
        const uint32_t start = sequence_.load(std::memory_order_relaxed);
        sequence_.store(start + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);
        std::memcpy(static_cast<void*>(&data_), &value, sizeof(T));
        std::atomic_thread_fence(std::memory_order_release);
        sequence_.store(start + 2, std::memory_order_release);
    }

    // Reader side (any task). Spins only while a publish is in flight.
    T read() const {
        T copy;
        while (!tryRead(copy)) {
        }
        return copy;
    }

    // One attempt: false when a publish was in progress or completed during the copy.
    bool tryRead(T& out) const {
        const uint32_t before = sequence_.load(std::memory_order_acquire);
        if ((before & 1u) != 0) {
            return false;
        }
        std::memcpy(static_cast<void*>(&out), &data_, sizeof(T));
        std::atomic_thread_fence(std::memory_order_acquire);
        const uint32_t after = sequence_.load(std::memory_order_relaxed);
        return before == after;
    }

    // Even, and increases by two per publish.
    uint32_t generation() const { return sequence_.load(std::memory_order_acquire); }

private:
    std::atomic<uint32_t> sequence_;
    T data_;
};

}  // namespace model
