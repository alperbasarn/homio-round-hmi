#include "ConnectivityManager.h"

#include <atomic>
#include <cstring>

#include "esp_coexist.h"
#include "esp_gap_ble_api.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"

namespace {
constexpr const char* TAG = "ConnMgr";

// Task sits between the display task (priority 2) and the network task
// (priority 3). Using priority 2 until the bands are spread further apart.
constexpr UBaseType_t kTaskPriority   = 2;
constexpr uint32_t    kStackSize      = 4096;
constexpr UBaseType_t kQueueDepth     = 16;
constexpr uint64_t    kConnectTimeoutUs  = 15'000'000ULL; // 15 s
constexpr uint64_t    kPortalDebounceUs  = 30'000'000ULL; // 30 s

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
        case ConnMgrEvent::EV_STA_ASSOCIATED:         return "EV_STA_ASSOCIATED";
        case ConnMgrEvent::EV_BT_SCAN_REQUESTED:      return "EV_BT_SCAN_REQUESTED";
        case ConnMgrEvent::EV_BT_ENABLE_REQUESTED:    return "EV_BT_ENABLE_REQUESTED";
        case ConnMgrEvent::EV_PORTAL_ENABLE_REQUESTED:return "EV_PORTAL_ENABLE_REQUESTED";
        case ConnMgrEvent::EV_BT_ADV_START:           return "EV_BT_ADV_START";
        case ConnMgrEvent::EV_BT_ADV_STOP:            return "EV_BT_ADV_STOP";
        case ConnMgrEvent::EV_BT_SCAN_START:          return "EV_BT_SCAN_START";
        case ConnMgrEvent::EV_BT_ADV_HOLD:            return "EV_BT_ADV_HOLD";
        case ConnMgrEvent::EV_BT_ADV_RELEASE:         return "EV_BT_ADV_RELEASE";
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

    // Give WiFi and BLE equal radio time. The default (ESP_COEX_PREFER_WIFI)
    // blocks all BLE TX slots; balance mode lets both radios share the air.
    esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);

    // Create the one-shot 15 s connect-timeout timer.
    const esp_timer_create_args_t timer_args = {
        .callback  = &ConnectivityManager::connectTimeoutCb_,
        .arg       = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name      = "ConnMgrTimeout",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&timer_args, &connect_timer_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create connect timeout timer");
        return ESP_FAIL;
    }

    // 30 s debounce: keeps PortalGuestActive while phone TCP sessions close.
    const esp_timer_create_args_t debounce_args = {
        .callback  = &ConnectivityManager::debounceTimerCb_,
        .arg       = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name      = "ConnMgrDebounce",
        .skip_unhandled_events = true,
    };
    if (esp_timer_create(&debounce_args, &debounce_timer_) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create portal debounce timer");
        return ESP_FAIL;
    }

    event_group_ = xEventGroupCreate();
    if (event_group_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create WiFi event group");
        return ESP_ERR_NO_MEM;
    }

    queue_ = xQueueCreate(kQueueDepth, sizeof(ConnMgrEvent));
    if (queue_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create event queue");
        return ESP_ERR_NO_MEM;
    }

    publish_mutex_ = xSemaphoreCreateMutex();
    if (publish_mutex_ == nullptr) {
        ESP_LOGE(TAG, "Failed to create publish mutex");
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
    // Serialises concurrent writers (runTask + patch* methods) before the
    // seqlock write.  Readers are still lock-free via getSnapshot().
    xSemaphoreTake(publish_mutex_, portMAX_DELAY);
    publishSnapshotUnlocked_(next);
    xSemaphoreGive(publish_mutex_);
}

void ConnectivityManager::publishSnapshotUnlocked_(const ConnectivitySnapshot& next) {
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

void ConnectivityManager::patchStaDetails(const char* ssid, const char* ip,
                                           int8_t rssi_dbm, uint8_t rssi_bars) {
    xSemaphoreTake(publish_mutex_, portMAX_DELAY);
    ConnectivitySnapshot next = snapshot_;
    strncpy(next.sta_ssid, ssid ? ssid : "", sizeof(next.sta_ssid) - 1);
    next.sta_ssid[sizeof(next.sta_ssid) - 1] = '\0';
    strncpy(next.sta_ip, ip ? ip : "", sizeof(next.sta_ip) - 1);
    next.sta_ip[sizeof(next.sta_ip) - 1] = '\0';
    next.rssi_dbm  = rssi_dbm;
    next.rssi_bars = rssi_bars;
    publishSnapshotUnlocked_(next);
    xSemaphoreGive(publish_mutex_);
}

void ConnectivityManager::patchApDetails(const char* ssid, const char* ip,
                                          const char* password) {
    xSemaphoreTake(publish_mutex_, portMAX_DELAY);
    ConnectivitySnapshot next = snapshot_;
    strncpy(next.ap_ssid, ssid ? ssid : "", sizeof(next.ap_ssid) - 1);
    next.ap_ssid[sizeof(next.ap_ssid) - 1] = '\0';
    strncpy(next.ap_ip, ip ? ip : "", sizeof(next.ap_ip) - 1);
    next.ap_ip[sizeof(next.ap_ip) - 1] = '\0';
    strncpy(next.ap_password, password ? password : "", sizeof(next.ap_password) - 1);
    next.ap_password[sizeof(next.ap_password) - 1] = '\0';
    publishSnapshotUnlocked_(next);
    xSemaphoreGive(publish_mutex_);
}

void ConnectivityManager::patchBtDetails(bool hid_connected, const char* hid_addr,
                                          bool serial_connected, const char* serial_addr) {
    xSemaphoreTake(publish_mutex_, portMAX_DELAY);
    ConnectivitySnapshot next = snapshot_;
    next.bt_hid_connected = hid_connected;
    strncpy(next.bt_hid_addr, hid_addr ? hid_addr : "", sizeof(next.bt_hid_addr) - 1);
    next.bt_hid_addr[sizeof(next.bt_hid_addr) - 1] = '\0';
    next.bt_serial_connected = serial_connected;
    strncpy(next.bt_serial_addr, serial_addr ? serial_addr : "", sizeof(next.bt_serial_addr) - 1);
    next.bt_serial_addr[sizeof(next.bt_serial_addr) - 1] = '\0';
    publishSnapshotUnlocked_(next);
    xSemaphoreGive(publish_mutex_);
}

// static
void ConnectivityManager::connMgrTask(void* arg) {
    static_cast<ConnectivityManager*>(arg)->runTask();
}

// ── WiFi stack lifecycle ─────────────────────────────────────────────────────

esp_err_t ConnectivityManager::initWifiStack() {
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t err = esp_wifi_init(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(APSTA) failed: %s", esp_err_to_name(err));
        return err;
    }
    err = esp_wifi_start();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(err));
        return err;
    }
    esp_wifi_set_ps(WIFI_PS_NONE);
    ESP_LOGI(TAG, "WiFi stack initialised");
    return ESP_OK;
}

void ConnectivityManager::stopWifiStack() {
    esp_wifi_stop();
    esp_wifi_deinit();
}

// ── Radio mutators ────────────────────────────────────────────────────────────

esp_err_t ConnectivityManager::configureAP(const wifi_config_t& cfg) {
    return esp_wifi_set_config(WIFI_IF_AP, const_cast<wifi_config_t*>(&cfg));
}

// T-08: non-blocking — arms 15 s timer, returns immediately.
esp_err_t ConnectivityManager::startSTAConnect(const wifi_config_t& cfg) {
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    const esp_err_t err = esp_wifi_set_config(WIFI_IF_STA, const_cast<wifi_config_t*>(&cfg));
    if (err != ESP_OK) {
        return err;
    }
    const esp_err_t cerr = esp_wifi_connect();
    if (cerr != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(cerr));
        // Post a synthetic timeout so runTask advances state without waiting.
        postEvent(ConnMgrEvent::EV_CONNECT_TIMEOUT);
        return cerr;
    }
    // Arm 15 s safety-net timer.
    esp_timer_stop(connect_timer_);
    esp_timer_start_once(connect_timer_, kConnectTimeoutUs);
    return ESP_OK;
}

// static — fires on esp-timer task; just posts the event.
void ConnectivityManager::connectTimeoutCb_(void* arg) {
    static_cast<ConnectivityManager*>(arg)->postEvent(ConnMgrEvent::EV_CONNECT_TIMEOUT);
}

void ConnectivityManager::cancelConnectTimer_() {
    if (connect_timer_ != nullptr) {
        esp_timer_stop(connect_timer_);
    }
}

// static
void ConnectivityManager::debounceTimerCb_(void* arg) {
    static_cast<ConnectivityManager*>(arg)->postEvent(ConnMgrEvent::EV_BACKOFF_TIMER);
}

void ConnectivityManager::cancelDebounceTimer_() {
    if (debounce_timer_ != nullptr) {
        esp_timer_stop(debounce_timer_);
    }
}

void ConnectivityManager::disconnectSTA() {
    esp_wifi_disconnect();
}

void ConnectivityManager::setWifiMode(wifi_mode_t mode) {
    esp_wifi_set_mode(mode);
}

wifi_mode_t ConnectivityManager::getWifiMode() {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);
    return mode;
}

esp_err_t ConnectivityManager::startNonBlockingScan() {
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.ssid             = nullptr;
    scan_cfg.bssid            = nullptr;
    scan_cfg.channel          = 0;
    scan_cfg.show_hidden      = true;
    scan_cfg.scan_type        = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;
    scan_cfg.scan_time.passive    = 0;
    return esp_wifi_scan_start(&scan_cfg, false);
}

esp_err_t ConnectivityManager::syncScan(std::vector<wifi_ap_record_t>& out) {
    out.clear();
    esp_wifi_set_mode(WIFI_MODE_APSTA);
    wifi_scan_config_t scan_cfg = {};
    scan_cfg.ssid             = nullptr;
    scan_cfg.bssid            = nullptr;
    scan_cfg.channel          = 0;
    scan_cfg.show_hidden      = true;
    scan_cfg.scan_type        = WIFI_SCAN_TYPE_ACTIVE;
    scan_cfg.scan_time.active.min = 100;
    scan_cfg.scan_time.active.max = 300;
    scan_cfg.scan_time.passive    = 0;
    if (esp_wifi_scan_start(&scan_cfg, true) != ESP_OK) {
        return ESP_FAIL;
    }
    uint16_t count = 20;
    out.resize(count);
    esp_err_t err = esp_wifi_scan_get_ap_records(&count, out.data());
    if (err == ESP_OK) {
        out.resize(count);
    } else {
        out.clear();
    }
    return err;
}

bool ConnectivityManager::getApStaList(wifi_sta_list_t* out) {
    return esp_wifi_ap_get_sta_list(out) == ESP_OK;
}

bool ConnectivityManager::getStaApInfo(wifi_ap_record_t* out) {
    return esp_wifi_sta_get_ap_info(out) == ESP_OK;
}

// ── BLE GAP delegation (T-06) ──────────────────────────────────────────────────────────

void ConnectivityManager::bleRequestAdvertisingStart(const esp_ble_adv_params_t& params) {
    ble_adv_params_ = params;
    postEvent(ConnMgrEvent::EV_BT_ADV_START);
}

void ConnectivityManager::bleRequestAdvertisingStop() {
    postEvent(ConnMgrEvent::EV_BT_ADV_STOP);
}

void ConnectivityManager::bleRequestScanStart(uint32_t durationSec) {
    ble_scan_duration_ = durationSec;
    postEvent(ConnMgrEvent::EV_BT_SCAN_START);
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
            case ConnMgrEvent::EV_STA_ASSOCIATED:
                // Boost WiFi priority during the DHCP window so EAPOL/DHCP
                // exchanges are not starved by concurrent BLE activity (T-11).
                esp_coex_preference_set(ESP_COEX_PREFER_WIFI);
                // Pause BLE advertising during the same window (T-15).
                if (!sta_adv_held_) {
                    sta_adv_held_ = true;
                    if (adv_hold_count_++ == 0) {
                        esp_ble_gap_stop_advertising();
                    }
                }
                break;
            case ConnMgrEvent::EV_STA_GOT_IP:
                cancelConnectTimer_();
                state_ = ConnMgrState::StaConnected;
                // Restore balanced coex now that the DHCP window is closed.
                esp_coex_preference_set(ESP_COEX_PREFER_BALANCE);
                // Release EAPOL hold; restart advertising if no other hold remains.
                if (sta_adv_held_) {
                    sta_adv_held_ = false;
                    if (adv_hold_count_ > 0 && --adv_hold_count_ == 0) {
                        esp_ble_gap_start_advertising(&ble_adv_params_);
                    }
                }
                break;
            case ConnMgrEvent::EV_STA_DISCONNECTED:
                if (state_ == ConnMgrState::StaConnected ||
                    state_ == ConnMgrState::StaConnecting) {
                    cancelConnectTimer_();
                    state_ = ConnMgrState::StaFailedBackoff;
                    // Release any EAPOL hold so advertising is not stuck paused.
                    if (sta_adv_held_) {
                        sta_adv_held_ = false;
                        if (adv_hold_count_ > 0 && --adv_hold_count_ == 0) {
                            esp_ble_gap_start_advertising(&ble_adv_params_);
                        }
                    }
                }
                break;
            case ConnMgrEvent::EV_AP_CLIENT_JOINED:
                cancelDebounceTimer_();  // client rejoined during debounce window
                ap_client_count_++;
                state_ = ConnMgrState::PortalGuestActive;
                break;
            case ConnMgrEvent::EV_AP_CLIENT_LEFT:
                if (ap_client_count_ > 0) {
                    ap_client_count_--;
                }
                if (ap_client_count_ == 0 &&
                    state_ == ConnMgrState::PortalGuestActive) {
                    // Arm 30 s debounce; stay in PortalGuestActive so update()
                    // does not trigger STA retries while the phone tears down.
                    ESP_LOGI(TAG, "Last AP client left; debounce 30 s before STA retry");
                    esp_timer_stop(debounce_timer_);
                    esp_timer_start_once(debounce_timer_, kPortalDebounceUs);
                }
                break;
            case ConnMgrEvent::EV_BACKOFF_TIMER:
                // Debounce expired: AP has been empty for 30 s, resume STA.
                if (state_ == ConnMgrState::PortalGuestActive && ap_client_count_ == 0) {
                    ESP_LOGI(TAG, "Portal debounce expired; resuming STA attempts");
                    state_ = ConnMgrState::StaFailedBackoff;
                }
                break;
            case ConnMgrEvent::EV_CONNECT_TIMEOUT:
                ESP_LOGW(TAG, "STA connect timed out");
                esp_wifi_disconnect();
                state_ = ConnMgrState::StaFailedBackoff;
                if (sta_adv_held_) {
                    sta_adv_held_ = false;
                    if (adv_hold_count_ > 0 && --adv_hold_count_ == 0) {
                        esp_ble_gap_start_advertising(&ble_adv_params_);
                    }
                }
                break;
            case ConnMgrEvent::EV_USER_REQUESTED_CONNECT:
                state_ = ConnMgrState::StaConnecting;
                break;
            case ConnMgrEvent::EV_BT_SCAN_REQUESTED:
                // Gate BLE scans while a STA association is in progress so
                // that radio contention does not break the 4-way handshake.
                if (state_ == ConnMgrState::StaConnecting) {
                    ESP_LOGW(TAG, "BLE scan rejected: STA association in progress");
                } else {
                    postEvent(ConnMgrEvent::EV_BT_SCAN_START);
                }
                break;
            case ConnMgrEvent::EV_BT_ADV_START: {
                const esp_err_t err = esp_ble_gap_start_advertising(&ble_adv_params_);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "BLE start advertising failed: %s", esp_err_to_name(err));
                }
                break;
            }
            case ConnMgrEvent::EV_BT_ADV_STOP: {
                const esp_err_t err = esp_ble_gap_stop_advertising();
                if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
                    ESP_LOGW(TAG, "BLE stop advertising failed: %s", esp_err_to_name(err));
                }
                break;
            }
            case ConnMgrEvent::EV_BT_SCAN_START: {
                const esp_err_t err =
                    esp_ble_gap_start_scanning(ble_scan_duration_);
                if (err != ESP_OK) {
                    ESP_LOGW(TAG, "BLE start scanning failed: %s", esp_err_to_name(err));
                }
                break;
            }
            case ConnMgrEvent::EV_BT_ADV_HOLD:
                ESP_LOGI(TAG, "ADV hold +1 → %d", adv_hold_count_ + 1);
                if (adv_hold_count_++ == 0) {
                    esp_ble_gap_stop_advertising();
                }
                break;
            case ConnMgrEvent::EV_BT_ADV_RELEASE:
                if (adv_hold_count_ <= 0) {
                    ESP_LOGW(TAG, "ADV hold release with count already 0");
                    break;
                }
                ESP_LOGI(TAG, "ADV hold -1 → %d", adv_hold_count_ - 1);
                if (--adv_hold_count_ == 0) {
                    esp_ble_gap_start_advertising(&ble_adv_params_);
                }
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

void ConnectivityManager::setBtEnabledFlag(bool enabled) {
    xSemaphoreTake(publish_mutex_, portMAX_DELAY);
    ConnectivitySnapshot next = snapshot_;
    next.bt_enabled = enabled;
    publishSnapshotUnlocked_(next);
    xSemaphoreGive(publish_mutex_);
}

void ConnectivityManager::requestAdvertisingHold(const char* reason) {
    ESP_LOGI(TAG, "requestAdvertisingHold: %s", reason ? reason : "");
    postEvent(ConnMgrEvent::EV_BT_ADV_HOLD);
}

void ConnectivityManager::releaseAdvertisingHold(const char* reason) {
    ESP_LOGI(TAG, "releaseAdvertisingHold: %s", reason ? reason : "");
    postEvent(ConnMgrEvent::EV_BT_ADV_RELEASE);
}

