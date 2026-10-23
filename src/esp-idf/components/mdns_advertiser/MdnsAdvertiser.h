#pragma once

#include <stdint.h>

class NVSManager;

// Advertises _qnob._tcp via mDNS when the device has a STA IP.
// Lifecycle: call start() on IP_EVENT_STA_GOT_IP, stop() on STA_LOST_IP.
class MdnsAdvertiser {
public:
    static constexpr int kTcpPort = 23456; // must match CommandServer::kPort

    MdnsAdvertiser() = default;
    ~MdnsAdvertiser() { stop(); }

    // Initialise mDNS and register the service.
    // mac[6] — raw MAC bytes (little-endian as returned by esp_wifi_get_mac).
    void start(const NVSManager* nvs, const uint8_t mac[6]);

    // Remove service; safe to call even if not started.
    void stop();

    // Update the "paired" TXT record in place (called by A6/A7).
    void updatePaired(bool paired);

    bool isRunning() const { return running_; }

private:
    bool running_  = false;
    bool paired_   = false;
    char hostname_[32] = {};
};
