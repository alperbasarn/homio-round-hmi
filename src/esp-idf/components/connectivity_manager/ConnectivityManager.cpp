#include "ConnectivityManager.h"

#include <atomic>

#include "esp_log.h"

namespace {
constexpr const char* TAG = "ConnMgr";
}

ConnectivityManager& ConnectivityManager::instance() {
    static ConnectivityManager manager;
    return manager;
}

esp_err_t ConnectivityManager::begin() {
    if (initialized_) {
        return ESP_OK;
    }
    initialized_ = true;

    // Seed the published snapshot once so version == 1 marks "ConnMgr has
    // been initialised at least once." Event-driven publishes from T-03+
    // will keep it up to date afterwards.
    publishSnapshot(ConnectivitySnapshot{});

    ESP_LOGI(TAG, "scaffold ready (T-02 snapshot reachable); task + queue arrive in T-03");
    return ESP_OK;
}

ConnectivitySnapshot ConnectivityManager::getSnapshot() const {
    // Classic seqlock reader. Loop until we observe the same even sequence
    // before and after copying. With a single writer there's never more
    // than one concurrent update, so the loop terminates quickly.
    ConnectivitySnapshot copy;
    uint32_t s1;
    uint32_t s2;
    do {
        s1 = seq_.load(std::memory_order_acquire);
        copy = snapshot_;
        std::atomic_thread_fence(std::memory_order_acquire);
        s2 = seq_.load(std::memory_order_acquire);
    } while ((s1 & 1u) != 0u || s1 != s2);
    return copy;
}

void ConnectivityManager::publishSnapshot(const ConnectivitySnapshot& next) {
    // Single-writer seqlock: bump to odd (update in progress), write the
    // snapshot, bump to even (stable). `version` advances by 1 per publish,
    // so seq_ == version * 2 once the writer is done.
    const uint32_t prev = seq_.load(std::memory_order_relaxed);
    seq_.store(prev + 1u, std::memory_order_release);

    snapshot_ = next;
    snapshot_.version = (prev / 2u) + 1u;

    std::atomic_thread_fence(std::memory_order_release);
    seq_.store(prev + 2u, std::memory_order_release);
}
