#include "MdnsAdvertiser.h"

#include <string.h>
#include <stdio.h>

#include "NVSManager.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_app_desc.h"
#include "mdns.h"

static const char* TAG = "MdnsAdv";

void MdnsAdvertiser::start(const NVSManager* nvs, const uint8_t mac[6]) {
    if (running_) {
        stop();
    }

    // ── Hostname: qnob-<last4mac> ──────────────────────────────────────────
    snprintf(hostname_, sizeof(hostname_), "qnob-%02x%02x", mac[4], mac[5]);

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_init failed: %s", esp_err_to_name(err));
        return;
    }
    mdns_hostname_set(hostname_);
    mdns_instance_name_set("Qnob");

    // ── Build TXT records ──────────────────────────────────────────────────
    const char* device_name = (nvs && !nvs->deviceName.empty())
                              ? nvs->deviceName.c_str() : "Qnob";

    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    const char* fw_version = esp_app_get_description()->version;

    // Reflect the persisted pairing state in the initial TXT record so the
    // companion can distinguish paired from unpaired devices at first sight.
    paired_ = (nvs != nullptr && nvs->isPaired);
    const char* paired_str = paired_ ? "1" : "0";

    // mdns_txt_item_t uses non-const char* — cast is safe, values are read-only.
    mdns_txt_item_t txt[] = {
        { (char*)"name",   (char*)device_name },
        { (char*)"fw",     (char*)fw_version  },
        { (char*)"mac",    (char*)mac_str      },
        { (char*)"caps",   (char*)"tcp,ble"   },
        { (char*)"proto",  (char*)"1"          },
        { (char*)"paired", (char*)paired_str   },
    };
    constexpr size_t kTxtCount = sizeof(txt) / sizeof(txt[0]);

    err = mdns_service_add(nullptr, "_qnob", "_tcp", kTcpPort, txt, kTxtCount);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mdns_service_add failed: %s", esp_err_to_name(err));
        mdns_free();
        return;
    }

    running_ = true;
    ESP_LOGI(TAG, "Advertising _qnob._tcp on %s.local:%d (name=%s fw=%s)",
             hostname_, kTcpPort, device_name, fw_version);
}

void MdnsAdvertiser::stop() {
    if (!running_) return;
    mdns_service_remove("_qnob", "_tcp");
    mdns_free();
    running_ = false;
    ESP_LOGI(TAG, "mDNS stopped");
}

void MdnsAdvertiser::updatePaired(bool paired) {
    if (!running_) return;
    if (paired_ == paired) return;
    paired_ = paired;
    mdns_service_txt_item_set("_qnob", "_tcp", "paired", paired ? "1" : "0");
    ESP_LOGI(TAG, "mDNS paired=%d", paired ? 1 : 0);
}
