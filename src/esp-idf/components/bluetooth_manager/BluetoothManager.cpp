#include "BluetoothManager.h"

#include "ConnectivityManager.h"

#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatt_common_api.h"
#include "esp_gatts_api.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

extern "C" {
#include "esp_hidd_prf_api.h"
#include "hid_dev.h"
}

#include <algorithm>
#include <cstring>
#include <cstdio>

namespace {

constexpr const char* TAG = "BluetoothManager";
constexpr size_t kMaxBleAdvNameLen = 16;
constexpr size_t kMaxBleScanRspNameLen = 29;
constexpr uint16_t kSerialAppId = 0x56;
constexpr uint16_t kSerialServiceUuid = 0xABF0;
constexpr uint16_t kSerialRxUuid = 0xABF1;
constexpr uint16_t kSerialTxUuid = 0xABF2;
constexpr uint16_t kSerialMaxLen = 512;
constexpr uint16_t kLocalMtu = 247;

enum SerialAttrIndex {
    SERIAL_IDX_SVC,
    SERIAL_IDX_RX_CHAR,
    SERIAL_IDX_RX_VAL,
    SERIAL_IDX_TX_CHAR,
    SERIAL_IDX_TX_VAL,
    SERIAL_IDX_TX_CFG,
    SERIAL_IDX_NB,
};

static const uint16_t primaryServiceUuid = ESP_GATT_UUID_PRI_SERVICE;
static const uint16_t charDeclarationUuid = ESP_GATT_UUID_CHAR_DECLARE;
static const uint16_t clientConfigUuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
static const uint8_t charPropWrite = ESP_GATT_CHAR_PROP_BIT_WRITE | ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
static const uint8_t charPropNotify = ESP_GATT_CHAR_PROP_BIT_READ | ESP_GATT_CHAR_PROP_BIT_NOTIFY;
static const uint8_t serialRxValue[20] = {0};
static const uint8_t serialTxValue[20] = {0};
static const uint8_t serialTxCcc[2] = {0, 0};

static const esp_gatts_attr_db_t serialGattDb[SERIAL_IDX_NB] = {
    [SERIAL_IDX_SVC] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&primaryServiceUuid, ESP_GATT_PERM_READ,
                               sizeof(kSerialServiceUuid), sizeof(kSerialServiceUuid), (uint8_t*)&kSerialServiceUuid}},
    [SERIAL_IDX_RX_CHAR] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&charDeclarationUuid, ESP_GATT_PERM_READ,
                               sizeof(charPropWrite), sizeof(charPropWrite), (uint8_t*)&charPropWrite}},
    [SERIAL_IDX_RX_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&kSerialRxUuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               kSerialMaxLen, sizeof(serialRxValue), (uint8_t*)serialRxValue}},
    [SERIAL_IDX_TX_CHAR] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&charDeclarationUuid, ESP_GATT_PERM_READ,
                               sizeof(charPropNotify), sizeof(charPropNotify), (uint8_t*)&charPropNotify}},
    [SERIAL_IDX_TX_VAL] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&kSerialTxUuid, ESP_GATT_PERM_READ,
                               kSerialMaxLen, sizeof(serialTxValue), (uint8_t*)serialTxValue}},
    [SERIAL_IDX_TX_CFG] =
        {{ESP_GATT_AUTO_RSP}, {ESP_UUID_LEN_16, (uint8_t*)&clientConfigUuid, ESP_GATT_PERM_READ | ESP_GATT_PERM_WRITE,
                               sizeof(uint16_t), sizeof(serialTxCcc), (uint8_t*)serialTxCcc}},
};

// Raw ADV payload base (11 bytes):
// - Flags: General Discoverable + BR/EDR Not Supported
// - Complete list of 16-bit service UUIDs: HID (0x1812)
// - Appearance: HID Keyboard (0x03C1)
static uint8_t hidAdvRawBase[] = {
    0x02, 0x01, 0x06,
    0x03, 0x03, 0x12, 0x18,
    0x03, 0x19, 0xC1, 0x03,
};

static uint8_t advRaw[31] = {0};
static uint8_t advRawLen = sizeof(hidAdvRawBase);

// Raw scan response with local name: [len][type=0x09][name...]
static uint8_t scanRspRaw[31] = {0};
static uint8_t scanRspRawLen = 0;

static esp_ble_adv_params_t advParams = {
    .adv_int_min = 0x40,   // 40 ms
    .adv_int_max = 0x60,   // 60 ms
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {0},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

static esp_ble_scan_params_t scanParams = {
    .scan_type          = BLE_SCAN_TYPE_ACTIVE,
    .own_addr_type      = BLE_ADDR_TYPE_PUBLIC,
    .scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL,
    .scan_interval      = 0x50,
    .scan_window        = 0x30,
    .scan_duplicate     = BLE_SCAN_DUPLICATE_ENABLE,
};

static std::string extractBtName(const uint8_t* data, uint8_t len) {
    for (uint8_t i = 0; i + 1 < len; ) {
        uint8_t fieldLen  = data[i];
        if (fieldLen == 0 || i + fieldLen >= len) break;
        uint8_t fieldType = data[i + 1];
        // 0x08 = Shortened Local Name, 0x09 = Complete Local Name
        if ((fieldType == 0x08 || fieldType == 0x09) && fieldLen > 1) {
            return std::string(reinterpret_cast<const char*>(&data[i + 2]), fieldLen - 1);
        }
        i += fieldLen + 1;
    }
    return "";
}

static std::string bleAdvertisedName(const std::string& desiredName) {
    if (desiredName.size() <= kMaxBleAdvNameLen) {
        return desiredName;
    }
    ESP_LOGW(TAG, "BLE name too long (%u), truncating to %u for ADV", static_cast<unsigned>(desiredName.size()),
             static_cast<unsigned>(kMaxBleAdvNameLen));
    return desiredName.substr(0, kMaxBleAdvNameLen);
}

BluetoothManager* activeManager = nullptr;

void hidEventCallback(esp_hidd_cb_event_t event, esp_hidd_cb_param_t* param) {
    if (activeManager != nullptr) {
        activeManager->onHidEvent(static_cast<int>(event), param);
    }
}

void gapEventCallback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* param) {
    if (activeManager != nullptr) {
        activeManager->onGapEvent(static_cast<int>(event), param);
    }
}

}  // namespace

extern "C" void bluetooth_manager_extra_gatts_event_handler(esp_gatts_cb_event_t event,
                                                             esp_gatt_if_t gattsIf,
                                                             esp_ble_gatts_cb_param_t* param) {
    if (activeManager != nullptr) {
        activeManager->onSerialGattEvent(static_cast<int>(event), static_cast<int>(gattsIf), param);
    }
}

BluetoothManager& BluetoothManager::instance() {
    static BluetoothManager manager;
    return manager;
}

esp_err_t BluetoothManager::begin(const char* deviceName) {
    if (initialized) {
        return ESP_OK;
    }

    if (deviceName != nullptr && deviceName[0] != '\0') {
        name = deviceName;
    }
    activeManager = this;

    esp_err_t err = initializeController();
    if (err != ESP_OK) {
        return err;
    }

    err = registerProfiles();
    if (err != ESP_OK) {
        return err;
    }

    configureSecurity();
    registerSerialProfile();

    err = esp_ble_gatt_set_local_mtu(kLocalMtu);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "set local MTU failed: %s", esp_err_to_name(err));
    }

    initialized = true;
    ready = true;

    // Defer advertising setup until HID profile registration is finished.
    // Calling GAP adv config too early can fail with ESP_ERR_INVALID_ARG.
    // HID REG_FINISH can arrive before begin() reaches this point, so retry
    // start here in case ADV data was already configured.
    if (enabled) {
        startAdvertising();
    }

    ESP_LOGI(TAG, "BLE HID + BLE serial initialized as '%s'", name.c_str());
    return ESP_OK;
}

esp_err_t BluetoothManager::initializeController() {
    esp_err_t err = esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "classic BT memory release failed: %s", esp_err_to_name(err));
    }

    esp_bt_controller_config_t btCfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    err = esp_bt_controller_init(&btCfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "BT controller init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "BT controller enable failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_init();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_bluedroid_enable();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t BluetoothManager::registerProfiles() {
    esp_err_t err = esp_ble_gap_register_callback(gapEventCallback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GAP callback register failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_hidd_profile_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HID profile init failed: %s", esp_err_to_name(err));
        return err;
    }

    err = esp_hidd_register_callbacks(hidEventCallback);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HID callback register failed: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

void BluetoothManager::registerSerialProfile() {
    esp_err_t err = esp_ble_gatts_app_register(kSerialAppId);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "BLE serial app register failed: %s", esp_err_to_name(err));
    }
}

void BluetoothManager::configureSecurity() {
    esp_ble_auth_req_t authReq = ESP_LE_AUTH_BOND;
    esp_ble_io_cap_t iocap = ESP_IO_CAP_NONE;
    uint8_t keySize = 16;
    uint8_t initKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
    uint8_t rspKey = ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;

    esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &authReq, sizeof(authReq));
    esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &iocap, sizeof(iocap));
    esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &keySize, sizeof(keySize));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &initKey, sizeof(initKey));
    esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &rspKey, sizeof(rspKey));
}

void BluetoothManager::configureAdvertising() {
    const std::string localName = bleAdvertisedName(name);
    const std::string advName = (localName.size() <= 18)
        ? localName
        : localName.substr(0, 18);
    const std::string scanRspName = (name.size() <= kMaxBleScanRspNameLen)
        ? name
        : name.substr(0, kMaxBleScanRspNameLen);

    std::memcpy(advRaw, hidAdvRawBase, sizeof(hidAdvRawBase));
    advRawLen = static_cast<uint8_t>(sizeof(hidAdvRawBase));
    if (!advName.empty()) {
        advRaw[advRawLen++] = static_cast<uint8_t>(advName.size() + 1);
        advRaw[advRawLen++] = 0x08;  // Shortened local name
        std::memcpy(&advRaw[advRawLen], advName.data(), advName.size());
        advRawLen = static_cast<uint8_t>(advRawLen + advName.size());
    }

    scanRspRaw[0] = static_cast<uint8_t>(scanRspName.size() + 1);
    scanRspRaw[1] = 0x09;  // Complete local name
    if (!scanRspName.empty()) {
        std::memcpy(&scanRspRaw[2], scanRspName.data(), scanRspName.size());
    }
    scanRspRawLen = static_cast<uint8_t>(scanRspName.size() + 2);

    esp_ble_gap_set_device_name(localName.c_str());
    // Override the Generic HID appearance set by hid_device_le_prf.c —
    // keyboard subtype is required for iOS and Windows to list the device.
    esp_ble_gap_config_local_icon(ESP_BLE_APPEARANCE_HID_KEYBOARD);
    if (enabled) {
        advDataConfigured = false;
        scanRspConfigured = false;
        scanRspConfigPending = true;

        const esp_err_t err = esp_ble_gap_config_adv_data_raw(advRaw, advRawLen);
        if (err != ESP_OK) {
            scanRspConfigPending = false;
            ESP_LOGW(TAG, "config adv raw failed: %s", esp_err_to_name(err));
        }
    }
}

void BluetoothManager::updateBatteryLevel(bool batteryConnected, float percentage) {
    uint8_t next = 0;
    if (batteryConnected && percentage >= 0.0f) {
        if (percentage > 100.0f) {
            percentage = 100.0f;
        }
        next = static_cast<uint8_t>(percentage + 0.5f);
    }

    if (next == hidBatteryLevel) {
        return;
    }

    hidBatteryLevel = next;
    esp_hidd_set_battery_level(hidBatteryLevel);
}

void BluetoothManager::startAdvertising() {
    if (!enabled || !ready || !advDataConfigured || !scanRspConfigured) {
        return;
    }
    ConnectivityManager::instance().bleRequestAdvertisingStart(advParams);
}

void BluetoothManager::stopAdvertising() {
    ConnectivityManager::instance().bleRequestAdvertisingStop();
}

void BluetoothManager::disconnectKnownPeers() {
    if (hidRemoteKnown) {
        esp_ble_gap_disconnect(hidRemoteBda);
    }
    if (serialRemoteKnown &&
        (!hidRemoteKnown || std::memcmp(serialRemoteBda, hidRemoteBda, sizeof(esp_bd_addr_t)) != 0)) {
        esp_ble_gap_disconnect(serialRemoteBda);
    }
}

static std::string bdaToString(const esp_bd_addr_t bda) {
    char buf[18];
    snprintf(buf, sizeof(buf), "%02X:%02X:%02X:%02X:%02X:%02X",
             bda[0], bda[1], bda[2], bda[3], bda[4], bda[5]);
    return buf;
}

std::string BluetoothManager::getConnectedHidAddress() const {
    return (hidConnected && hidRemoteKnown) ? bdaToString(hidRemoteBda) : "";
}

std::string BluetoothManager::getConnectedSerialAddress() const {
    return (serialConnected && serialRemoteKnown) ? bdaToString(serialRemoteBda) : "";
}

esp_err_t BluetoothManager::disconnectDevice(const std::string& address) {
    const std::string upper = [&]{ std::string s = address; for (char& c : s) c = toupper(c); return s; }();
    bool found = false;
    if (hidRemoteKnown && bdaToString(hidRemoteBda) == upper) {
        esp_ble_gap_disconnect(hidRemoteBda);
        found = true;
    }
    if (serialRemoteKnown && bdaToString(serialRemoteBda) == upper) {
        if (!found || std::memcmp(serialRemoteBda, hidRemoteBda, sizeof(esp_bd_addr_t)) != 0) {
            esp_ble_gap_disconnect(serialRemoteBda);
        }
        found = true;
    }
    return found ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t BluetoothManager::setEnabled(bool enable) {
    enabled = enable;
    if (!initialized) {
        return ESP_OK;
    }

    if (enabled) {
        configureAdvertising();
        startAdvertising();
        ESP_LOGI(TAG, "Bluetooth enabled");
    } else {
        stopAdvertising();
        disconnectKnownPeers();
        ESP_LOGI(TAG, "Bluetooth disabled");
    }
    return ESP_OK;
}

void BluetoothManager::onHidEvent(int event, void* rawParam) {
    auto* param = static_cast<esp_hidd_cb_param_t*>(rawParam);
    switch (static_cast<esp_hidd_cb_event_t>(event)) {
        case ESP_HIDD_EVENT_REG_FINISH:
            if (param != nullptr && param->init_finish.state == ESP_HIDD_INIT_OK) {
                configureAdvertising();
            } else {
                const int st = (param != nullptr) ? static_cast<int>(param->init_finish.state) : -1;
                ESP_LOGW(TAG, "HID profile registration failed, state=%d", st);
            }
            break;
        case ESP_HIDD_EVENT_BLE_CONNECT:
            hidConnected = true;
            if (param != nullptr) {
                hidConnId = param->connect.conn_id;
                hidRemoteKnown = true;
                std::memcpy(hidRemoteBda, param->connect.remote_bda, sizeof(hidRemoteBda));
            }
            // iOS/iPadOS often reads battery level immediately after HID connect.
            // Force-refresh BAS value on connect so the host does not keep the
            // default profile value if early updates happened before BAS ready.
            esp_hidd_set_battery_level(hidBatteryLevel == 0xff ? 0 : hidBatteryLevel);
            if (!enabled) {
                disconnectKnownPeers();
            }
            ESP_LOGI(TAG, "BLE HID connected");
            {
                const std::string ha = hidRemoteKnown    ? bdaToString(hidRemoteBda)    : "";
                const std::string sa = serialRemoteKnown ? bdaToString(serialRemoteBda) : "";
                ConnectivityManager::instance().patchBtDetails(
                    true, ha.c_str(), serialConnected, sa.c_str());
            }
            break;
        case ESP_HIDD_EVENT_BLE_DISCONNECT:
            hidConnected = false;
            hidSecure = false;
            hidRemoteKnown = false;
            hidConnId = 0xffff;
            ESP_LOGI(TAG, "BLE HID disconnected");
            startAdvertising();
            {
                const std::string sa = serialRemoteKnown ? bdaToString(serialRemoteBda) : "";
                ConnectivityManager::instance().patchBtDetails(
                    false, "", serialConnected, sa.c_str());
            }
            break;
        default:
            break;
    }
}

void BluetoothManager::onGapEvent(int event, void* rawParam) {
    auto* param = static_cast<esp_ble_gap_cb_param_t*>(rawParam);
    switch (static_cast<esp_gap_ble_cb_event_t>(event)) {
        case ESP_GAP_BLE_ADV_DATA_RAW_SET_COMPLETE_EVT:
            if (param != nullptr && param->adv_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "adv raw data set failed, status=%u", param->adv_data_raw_cmpl.status);
                break;
            }
            advDataConfigured = true;
            if (scanRspConfigPending) {
                scanRspConfigPending = false;
                const esp_err_t err = esp_ble_gap_config_scan_rsp_data_raw(scanRspRaw, scanRspRawLen);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG, "scan rsp raw config failed: %s", esp_err_to_name(err));
                    scanRspConfigured = true;
                    startAdvertising();
                }
            } else {
                scanRspConfigured = true;
                startAdvertising();
            }
            break;
        case ESP_GAP_BLE_SCAN_RSP_DATA_RAW_SET_COMPLETE_EVT:
            if (param != nullptr && param->scan_rsp_data_raw_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "scan rsp raw set failed, status=%u", param->scan_rsp_data_raw_cmpl.status);
                break;
            }
            scanRspConfigured = true;
            startAdvertising();
            break;
        case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
            if (param != nullptr && param->adv_data_cmpl.status != ESP_BT_STATUS_SUCCESS) {
                ESP_LOGE(TAG, "adv data set failed, status=%u", param->adv_data_cmpl.status);
                break;
            }
            advDataConfigured = true;
            startAdvertising();
            break;
        case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
            if (param != nullptr && param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS) {
                ESP_LOGI(TAG, "BLE advertising started");
            } else {
                const uint8_t status = (param != nullptr) ? param->adv_start_cmpl.status : 0xFF;
                ESP_LOGE(TAG, "BLE advertising failed to start, status=%u", status);
            }
            break;
        case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
            if (scanPending) {
                scanPending = false;
                scanStatusMsg = "setting_params";
                const esp_err_t err = esp_ble_gap_set_scan_params(&scanParams);
                if (err != ESP_OK) {
                    scanning = false;
                    scanStatusMsg = std::string("set_params_failed:") + esp_err_to_name(err);
                    ESP_LOGE(TAG, "set scan params failed: %s", esp_err_to_name(err));
                    configureAdvertising();
                }
            }
            break;
        case ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT:
            if (scanning) {
                const uint8_t status = (param != nullptr) ? param->scan_param_cmpl.status : 0xFF;
                if (status == ESP_BT_STATUS_SUCCESS) {
                    scanStatusMsg = "scanning";
                    ConnectivityManager::instance().bleRequestScanStart(
                        static_cast<uint32_t>(scanDuration));
                } else {
                    scanning = false;
                    char buf[32];
                    snprintf(buf, sizeof(buf), "param_status_err:%u", status);
                    scanStatusMsg = buf;
                    ESP_LOGE(TAG, "scan param set status error: %u", status);
                    configureAdvertising();
                }
            }
            break;
        case ESP_GAP_BLE_SCAN_RESULT_EVT: {
            if (!scanning || param == nullptr) break;
            const auto& r = param->scan_rst;
            if (r.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) break;
            char addr[18];
            snprintf(addr, sizeof(addr), "%02X:%02X:%02X:%02X:%02X:%02X",
                     r.bda[0], r.bda[1], r.bda[2], r.bda[3], r.bda[4], r.bda[5]);
            const std::string addrStr(addr);
            for (const auto& existing : lastScanResults) {
                if (existing.address == addrStr) goto scan_result_done;
            }
            {
                std::string devName = extractBtName(r.ble_adv, r.adv_data_len + r.scan_rsp_len);
                lastScanResults.push_back({addrStr, devName, r.rssi});
            }
            scan_result_done:;
            break;
        }
        case ESP_GAP_BLE_SCAN_STOP_COMPLETE_EVT:
            scanning = false;
            {
                char buf[48];
                snprintf(buf, sizeof(buf), "done:%d", static_cast<int>(lastScanResults.size()));
                scanStatusMsg = buf;
            }
            ESP_LOGI(TAG, "BLE scan complete, found %d device(s)", static_cast<int>(lastScanResults.size()));
            // Restart advertising now that scan is done
            configureAdvertising();
            break;
        case ESP_GAP_BLE_SEC_REQ_EVT:
            if (param != nullptr) {
                esp_ble_gap_security_rsp(param->ble_security.ble_req.bd_addr, true);
            }
            break;
        case ESP_GAP_BLE_AUTH_CMPL_EVT:
            if (param != nullptr) {
                hidSecure = param->ble_security.auth_cmpl.success;
                ESP_LOGI(TAG, "BLE pair status: %s", hidSecure ? "success" : "failed");
            }
            break;
        default:
            break;
    }
}

void BluetoothManager::onSerialGattEvent(int event, int gattsIf, void* rawParam) {
    auto* param = static_cast<esp_ble_gatts_cb_param_t*>(rawParam);
    if (param == nullptr) {
        return;
    }

    switch (static_cast<esp_gatts_cb_event_t>(event)) {
        case ESP_GATTS_REG_EVT:
            if (param->reg.status == ESP_GATT_OK && param->reg.app_id == kSerialAppId) {
                serialGattsIf = gattsIf;
                esp_ble_gatts_create_attr_tab(serialGattDb, gattsIf, SERIAL_IDX_NB, 0);
            }
            break;
        case ESP_GATTS_CREAT_ATTR_TAB_EVT:
            if (param->add_attr_tab.num_handle == SERIAL_IDX_NB &&
                param->add_attr_tab.svc_uuid.uuid.uuid16 == kSerialServiceUuid &&
                param->add_attr_tab.status == ESP_GATT_OK) {
                std::memcpy(serialHandles, param->add_attr_tab.handles, sizeof(serialHandles));
                esp_ble_gatts_start_service(serialHandles[SERIAL_IDX_SVC]);
                ESP_LOGI(TAG, "BLE serial service started");
            }
            break;
        case ESP_GATTS_CONNECT_EVT:
            if (gattsIf == serialGattsIf) {
                serialConnected = true;
                serialConnId = param->connect.conn_id;
                serialRemoteKnown = true;
                std::memcpy(serialRemoteBda, param->connect.remote_bda, sizeof(serialRemoteBda));
                if (!enabled) {
                    disconnectKnownPeers();
                }
                ESP_LOGI(TAG, "BLE serial connected");
                {
                    const std::string ha = hidRemoteKnown    ? bdaToString(hidRemoteBda)    : "";
                    const std::string sa = serialRemoteKnown ? bdaToString(serialRemoteBda) : "";
                    ConnectivityManager::instance().patchBtDetails(
                        hidConnected, ha.c_str(), true, sa.c_str());
                }
            }
            break;
        case ESP_GATTS_DISCONNECT_EVT:
            if (gattsIf == serialGattsIf) {
                serialConnected = false;
                serialNotifyEnabled = false;
                serialRemoteKnown = false;
                serialConnId = 0xffff;
                serialMtu = 23;
                ESP_LOGI(TAG, "BLE serial disconnected");
                {
                    const std::string ha = hidRemoteKnown ? bdaToString(hidRemoteBda) : "";
                    ConnectivityManager::instance().patchBtDetails(
                        hidConnected, ha.c_str(), false, "");
                }
            }
            break;
        case ESP_GATTS_MTU_EVT:
            if (gattsIf == serialGattsIf) {
                serialMtu = param->mtu.mtu;
            }
            break;
        case ESP_GATTS_WRITE_EVT:
            if (gattsIf == serialGattsIf && !param->write.is_prep) {
                if (param->write.handle == serialHandles[SERIAL_IDX_TX_CFG] && param->write.len == 2) {
                    const uint16_t cfg = param->write.value[0] | (param->write.value[1] << 8);
                    serialNotifyEnabled = (cfg & 0x0001) != 0;
                } else if (param->write.handle == serialHandles[SERIAL_IDX_RX_VAL]) {
                    ESP_LOGI(TAG, "BLE serial RX %u bytes", static_cast<unsigned>(param->write.len));
                }
            }
            break;
        default:
            break;
    }
}

esp_err_t BluetoothManager::restartAdvertising() {
    if (!initialized || !ready) {
        return ESP_ERR_INVALID_STATE;
    }
    configureAdvertising();
    return ESP_OK;
}

esp_err_t BluetoothManager::clearBonds() {
    if (!initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    const int count = esp_ble_get_bond_device_num();
    if (count <= 0) {
        return ESP_OK;
    }
    esp_ble_bond_dev_t* devices = new esp_ble_bond_dev_t[count];
    int actual = count;
    const esp_err_t err = esp_ble_get_bond_device_list(&actual, devices);
    if (err == ESP_OK) {
        for (int i = 0; i < actual; ++i) {
            esp_ble_remove_bond_device(devices[i].bd_addr);
        }
        ESP_LOGI(TAG, "Cleared %d bonded device(s)", actual);
    }
    delete[] devices;
    return err;
}

esp_err_t BluetoothManager::startScan(int durationSec) {
    if (!initialized || !ready) {
        scanStatusMsg = "not_ready";
        return ESP_ERR_INVALID_STATE;
    }
    lastScanResults.clear();
    scanDuration = durationSec;
    scanStatusMsg = "stopping_adv";
    scanning = true;
    scanPending = true;
    // Stop advertising first; scan params are set in ADV_STOP_COMPLETE_EVT
    // to avoid a state conflict when advertising stop is still in progress.
    ConnectivityManager::instance().bleRequestAdvertisingStop();
    return ESP_OK;
}

std::vector<BtScanResult> BluetoothManager::getScanResults() const {
    return lastScanResults;
}

int BluetoothManager::getBondedDeviceCount() const {
    if (!initialized) {
        return 0;
    }
    const int count = esp_ble_get_bond_device_num();
    return count < 0 ? 0 : count;
}

esp_err_t BluetoothManager::sendPlayPause() {
    if (!enabled || !ready || !hidConnected || hidConnId == 0xffff) {
        ESP_LOGW(TAG, "play/pause ignored: BLE HID is not connected");
        return ESP_ERR_INVALID_STATE;
    }

    if (!hidSecure) {
        ESP_LOGW(TAG, "play/pause: HID link is connected but not marked secure yet; sending anyway");
    }

    esp_hidd_send_consumer_value(hidConnId, HID_CONSUMER_PLAY_PAUSE, true);
    vTaskDelay(pdMS_TO_TICKS(30));
    esp_hidd_send_consumer_value(hidConnId, HID_CONSUMER_PLAY_PAUSE, false);
    return ESP_OK;
}

esp_err_t BluetoothManager::sendSerial(const uint8_t* data, size_t length) {
    if (!enabled || !ready || !serialConnected || !serialNotifyEnabled || serialConnId == 0xffff ||
        serialGattsIf == 0xff || data == nullptr || length == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    const size_t maxChunk = std::max<size_t>(1, serialMtu > 3 ? serialMtu - 3 : 20);
    size_t offset = 0;
    while (offset < length) {
        const size_t chunk = std::min(maxChunk, length - offset);
        const esp_err_t err = esp_ble_gatts_send_indicate(
            serialGattsIf,
            serialConnId,
            serialHandles[SERIAL_IDX_TX_VAL],
            static_cast<uint16_t>(chunk),
            const_cast<uint8_t*>(data + offset),
            false);
        if (err != ESP_OK) {
            return err;
        }
        offset += chunk;
    }
    return ESP_OK;
}

esp_err_t BluetoothManager::sendSerialLine(const std::string& line) {
    std::string payload = line;
    payload += "\r\n";
    return sendSerial(reinterpret_cast<const uint8_t*>(payload.data()), payload.size());
}
