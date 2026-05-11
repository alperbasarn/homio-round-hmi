#include "ConnectivityManager.h"

#include <atomic>

#include "esp_log.h"

namespace {
constexpr const char* TAG = "ConnMgr";

// Task sits between the display task (priority 2) and the network task
// (priority 3). Using priority 2 until the bands are spread further apart.
constexpr UBaseType_t kTaskPriority = 2;
constexpr uint32_t    kStackSize    = 4096;
constexpr UBaseType_t kQueueDepth   = 16;

const char* eventName(ConnMgrEvent ev) {
    switch (ev) {
        case ConnMgrEvent::EV_BOOT_DONE:              return "EV_BOOT_DONE";
        case ConnMgrEvent::EV_STA_GOT_IP:             return "EV_STA_GOT_IP";
        case ConnMgrEvent::EV_STA_DISCONNECTED:       return "EV_STA_DISCONNECTED";
        case ConnMgrEvent::EV_AP_CLIENT_JOINED:       return "EV_AP_CLIENT_JOINED";
        case ConnMgrEvent::EV_AP_CLIENT_LEFT:         return "EV_AP_CLIENT_LEFT";
        case ConnMgrEvent::EV_USER_REQUESTED_CONNECT: return "EV_USER_REQUESTED_CONNECT";
        case ConnMgrEvent::EV_BACKOFF_TIMER:          return "EV_BACKOFF_TIMER";
        case ConnMgrEvent::EV_CONNECT_TIMEOUT:        return "EV_CONNECT_TIMEOUT";
        case ConnMgrEvent::EV_BT_SCAN_REQUESTED:      return "EV_BT_SCAN_REQUESTED";
        case ConnMgrEvent::EV_BT_ENABLE_REQUESTED:    return "EV_BT_ENABLE_REQUESTED";
        case ConnMgrEvent::EV_PORTAL_ENABLE_REQUESTED:return "EV_PORTAL_ENABLE_REQUESTED";
        default:                                      return "EV_UNKNOWN";
    }
}

const char* stateName(ConnMgrState s) {
    switch (s) {
        case ConnMgrState::Boot:              return "BOOT";
        case ConnMgrState::ApReady:           return "AP_READY";
        case ConnMgrState::StaConnecting:     return "STA_CONNECTING";
        case ConnMgrState::StaConnected:      return "STA_CONNECTED";
        case ConnMgrState::StaFailedBackoff:  return "STA_FAILED_BACKOFF";
        case ConnMgrState::PortalGuestActive: return "PORTAL_GUEST_ACTIVE";
        default:                              return "UNKNOWN";
    }
}
}  // namespace

ConnectivityManager& ConnectivityManager::instance() {
    static ConnectivityManager manager;
    return manager;
}

esp_err_t ConnectivityManager::begin() {
    if (initialized_) {
        return ESP_OK;
    }
    initialized_ = true;

    queue_ = xQueueCreate(kQueueDepth, sizeof(ConnMgrEvent));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    const BaseType_t ret = xTaskCreate(
        connMgrTask, "ConnMgr", kStackSize, this, kTaskPriority, &taskHandle_);
    if (ret != pdPASS || taskHandle_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create ConnMgr task");
        return ESP_FAIL;
    }

    // Seed the published snapshot once so version == 1 marks "ConnMgr has
    // been initialised." Event-driven publishes keep it up to date afterwards.
    publishSnapshot(ConnectivitySnapshot{});

    ESP_LOGI(TAG, "task started; state=%s", stateName(state_));
    postEvent(ConnMgrEvent::EV_BOOT_DONE);
    return ESP_OK;
}

bool ConnectivityManager::postEvent(ConnMgrEvent ev) {
    if (queue_ == nullptr) {
        return false;
    }
    return xQueueSend(queue_, &ev, 0) == pdTRUE;
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

// static
void ConnectivityManager::connMgrTask(void* arg) {
    static_cast<ConnectivityManager*>(arg)->runTask();
}

void ConnectivityManager::runTask() {
    ESP_LOGI(TAG, "running on core %d", xPortGetCoreID());

    ConnMgrEvent ev;
    while (true) {
        if (xQueueReceive(queue_, &ev, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        const ConnMgrState prev        = state_;
        const uint8_t      prev_clients = ap_client_count_;

        // Receive events and drive the state machine. Radio calls arrive in T-05+.
        switch (ev) {
            case ConnMgrEvent::EV_BOOT_DONE:
                state_ = ConnMgrState::ApReady;
                break;
            case ConnMgrEvent::EV_STA_GOT_IP:
                state_ = ConnMgrState::StaConnected;
                break;
            case ConnMgrEvent::EV_STA_DISCONNECTED:
                if (state_ == ConnMgrState::StaConnected ||
                    state_ == ConnMgrState::StaConnecting) {
                    state_ = ConnMgrState::StaFailedBackoff;
                }
                break;
            case ConnMgrEvent::EV_AP_CLIENT_JOINED:
                ap_client_count_++;
                state_ = ConnMgrState::PortalGuestActive;
                break;
            case ConnMgrEvent::EV_AP_CLIENT_LEFT:
                if (ap_client_count_ > 0) {
                    ap_client_count_--;
                }
                if (ap_client_count_ == 0 &&
                    state_ == ConnMgrState::PortalGuestActive) {
                    state_ = ConnMgrState::StaFailedBackoff;
                }
                break;
            case ConnMgrEvent::EV_CONNECT_TIMEOUT:
                state_ = ConnMgrState::StaFailedBackoff;
                break;
            case ConnMgrEvent::EV_USER_REQUESTED_CONNECT:
                state_ = ConnMgrState::StaConnecting;
                break;
            default:
                break;
        }

        // Publish updated snapshot whenever state or client count changed.
        if (state_ != prev || ap_client_count_ != prev_clients) {
            ConnectivitySnapshot next = snapshot_;
            next.wifi_state = state_;
            next.ap_clients = ap_client_count_;
            publishSnapshot(next);
            ESP_LOGI(TAG, "%s → %s  [%s]  ap_clients=%u",
                     stateName(prev), stateName(state_),
                     eventName(ev), ap_client_count_);
        } else {
            ESP_LOGI(TAG, "event %s  (state unchanged: %s)",
                     eventName(ev), stateName(state_));
        }
    }
}

