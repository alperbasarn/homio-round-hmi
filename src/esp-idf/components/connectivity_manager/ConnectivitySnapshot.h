#pragma once

#include <cstdint>
#include <type_traits>

// Read-only snapshot of every WiFi + Bluetooth + portal state worth exposing
// outside the radio owner. Published by ConnectivityManager (single writer)
// and consumed by display, HTTP portal handlers, command routing, etc.
// (multi-reader). Designed to be a trivially copyable POD so the seqlock
// reader can copy it without locks.
//
// ── Stability contract ──────────────────────────────────────────────────────
//   Adding a field at the END is allowed (binary-compatible for in-tree
//   readers, just rebuild). Renaming, removing, reordering, or changing the
//   semantics of an existing field requires an explicit migration ticket so
//   downstream readers can be updated in lockstep.
struct ConnectivitySnapshot {
    enum class WifiState : uint8_t {
        Boot = 0,                // ConnMgr not yet initialised
        ApReady,                 // AP up, STA idle / not configured
        StaConnecting,           // EAPOL / DHCP in progress
        StaConnected,            // STA has IP
        StaFailedBackoff,        // STA attempts exhausted; waiting before retry
        PortalGuestActive,       // ≥1 client on AP; STA retries deferred
    };

    // ── WiFi runtime ────────────────────────────────────────────────────────
    WifiState wifi_state = WifiState::Boot;
    char  sta_ssid[33]   = {};   // 32-byte SSID + NUL
    char  sta_ip[16]     = {};   // dotted-quad + NUL ("255.255.255.255")
    char  ap_ssid[33]    = {};
    char  ap_ip[16]      = {};
    char  ap_password[65] = {}; // WPA2 max 63 chars + NUL; empty string for open AP
    uint8_t ap_clients   = 0;
    int8_t  rssi_dbm     = -100; // -100 means "no signal / unknown"
    uint8_t rssi_bars    = 0;    // 0..4, phone-bar style

    // ── Enable flags (NVS-backed once T-16 lands; defaults true at boot) ───
    bool wifi_sta_enabled = true;
    bool wifi_ap_enabled  = true;
    bool portal_enabled   = true;
    bool bt_enabled       = true;

    // ── Bluetooth runtime ───────────────────────────────────────────────────
    bool bt_hid_connected    = false;
    bool bt_serial_connected = false;
    bool bt_scanning         = false;
    char bt_hid_addr[18]     = {}; // "XX:XX:XX:XX:XX:XX" + NUL; empty when not connected
    char bt_serial_addr[18]  = {}; // "XX:XX:XX:XX:XX:XX" + NUL; empty when not connected

    // ── Diagnostics ─────────────────────────────────────────────────────────
    char     last_error[64] = {};
    uint32_t version        = 0; // monotonic; bumped on every publish
};

// Seqlock reads memcpy this struct under contention, so trivial copyability
// is a hard requirement. The static_assert fires at compile time if any
// future field breaks the contract.
static_assert(std::is_trivially_copyable<ConnectivitySnapshot>::value,
              "ConnectivitySnapshot must remain trivially copyable for seqlock reads");
