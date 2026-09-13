#pragma once
// FrameRing (LED-03, the MEDIA-06 shape): a fixed single-producer / single-consumer ring of
// text frames. The MQTT event task pushes a topic and a payload as they arrive; the display task
// pops them at the top of its frame. Acquire and release on the two indices are the whole
// synchronisation; nothing allocates; a full ring or an oversized frame drops the message and
// counts it, never blocks.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace adapters {

template <size_t Capacity, size_t TopicCapacity, size_t PayloadCapacity>
class FrameRing {
public:
    static_assert(Capacity >= 2, "a ring needs at least two slots");
    struct Frame {
        char topic[TopicCapacity];
        char payload[PayloadCapacity];
    };

    FrameRing() : head_(0), tail_(0), dropped_(0), oversized_(0), highWater_(0) {}

    // Producer. False when the frame does not fit or the ring is full (both counted).
    bool push(const char* topic, size_t topicLen, const char* payload, size_t payloadLen) {
        if (topic == nullptr || payload == nullptr || topicLen + 1 > TopicCapacity || payloadLen + 1 > PayloadCapacity) {
            oversized_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t next = (tail + 1) % Capacity;
        if (next == head_.load(std::memory_order_acquire)) {
            dropped_.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        Frame& f = slots_[tail];
        std::memcpy(f.topic, topic, topicLen);
        f.topic[topicLen] = '\0';
        std::memcpy(f.payload, payload, payloadLen);
        f.payload[payloadLen] = '\0';
        tail_.store(next, std::memory_order_release);
        const size_t depth = size();
        size_t seen = highWater_.load(std::memory_order_relaxed);
        while (depth > seen && !highWater_.compare_exchange_weak(seen, depth, std::memory_order_relaxed)) {
        }
        return true;
    }

    // Consumer.
    bool pop(Frame& out) {
        const size_t head = head_.load(std::memory_order_relaxed);
        if (head == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        out = slots_[head];
        head_.store((head + 1) % Capacity, std::memory_order_release);
        return true;
    }

    size_t size() const {
        const size_t head = head_.load(std::memory_order_acquire);
        const size_t tail = tail_.load(std::memory_order_acquire);
        return tail >= head ? tail - head : Capacity - head + tail;
    }
    bool empty() const { return size() == 0; }
    static constexpr size_t capacity() { return Capacity - 1; }  // one slot stays free
    uint32_t dropped() const { return dropped_.load(std::memory_order_relaxed); }
    uint32_t oversized() const { return oversized_.load(std::memory_order_relaxed); }
    size_t highWater() const { return highWater_.load(std::memory_order_relaxed); }

private:
    Frame slots_[Capacity];
    std::atomic<size_t> head_;
    std::atomic<size_t> tail_;
    std::atomic<uint32_t> dropped_;
    std::atomic<uint32_t> oversized_;
    std::atomic<size_t> highWater_;
};

}  // namespace adapters
