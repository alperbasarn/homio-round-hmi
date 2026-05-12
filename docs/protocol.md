# Qnob Protocol Specification

**Version:** 1 (`proto=1`)  
**Transports:** TCP (STA mode) · BLE Nordic-UART (always)  
**Encoding:** UTF-8 JSON  

---

## 1. Envelope schema

### 1.1 Request (client → device)

```json
{
  "id":     42,
  "cmd":    "setBrightness",
  "params": { "percent": 75 },
  "auth":   "base64-token-here"
}
```

| Field    | Type   | Required | Notes |
|----------|--------|----------|-------|
| `id`     | uint32 | yes      | Monotonically increasing per session; echoed in response for correlation |
| `cmd`    | string | yes      | Command name (see §4) |
| `params` | object | no       | Command-specific parameters; omit or `{}` when none |
| `auth`   | string | conditional | Required once pairing is active (`CONFIG_QNOB_REQUIRE_AUTH=y`). Omitted for `pair` and `status` commands |

### 1.2 Response (device → client)

```json
{
  "id":     42,
  "status": "ok",
  "data":   { "percent": 75 }
}
```

```json
{
  "id":     42,
  "status": "err",
  "error":  "bad_params",
  "detail": "percent must be 0-100"
}
```

| Field    | Type   | Always present | Notes |
|----------|--------|----------------|-------|
| `id`     | uint32 | yes            | Matches request `id` |
| `status` | string | yes            | `"ok"` or `"err"` |
| `data`   | object | on ok          | Command-specific payload; omitted when empty |
| `error`  | string | on err         | Error code (see §3) |
| `detail` | string | on err         | Human-readable hint; not for programmatic use |

### 1.3 Notification (device → client, unsolicited)

```json
{
  "type":  "notification",
  "event": "log",
  "data":  { "level": "info", "tag": "WiFi", "msg": "Connected to MySSID" }
}
```

Notifications carry no `id`. The client must not send a response. Events defined in §5.

---

## 2. Transport framing

### 2.1 TCP

- Port: **23456** (configurable via `CONFIG_QNOB_TCP_PORT`)
- Framing: **newline-delimited JSON** — each envelope is one UTF-8 line terminated by `\n`
- Max frame size: **65 535 bytes** per line; larger frames rejected with `error: "frame_too_large"`
- Clients: max 2 concurrent
- Heartbeat: client may send `{"id":N,"cmd":"ping"}` at any interval; device responds within 1 s or closes the connection

### 2.2 BLE Nordic-UART

- Service UUID: `0000ABF0-0000-1000-8000-00805F9B34FB`
- RX characteristic: `0000ABF1-0000-1000-8000-00805F9B34FB` (Write without response)
- TX characteristic: `0000ABF2-0000-1000-8000-00805F9B34FB` (Notify)
- Framing: **2-byte little-endian length prefix** + JSON payload, split across ATT writes/notifies of `MTU-3` bytes
- Max frame size: **16 384 bytes**; larger frames rejected with `error: "frame_too_large"`
- Clients: 1 concurrent command-channel central
- MTU: negotiated on connect; device uses the negotiated value

**BLE chunk format (multi-write reassembly):**

```
Write 1:  [total_len_lo][total_len_hi][json_bytes_0..MTU-5]
Write 2+: [json_bytes_N..MTU-3]
```

The length prefix appears only in the first write. Receiver accumulates writes until `total_len` bytes are received, then parses.

---

## 3. Error codes

| Code             | Meaning |
|------------------|---------|
| `unauth`         | Missing or invalid `auth` token |
| `already_paired` | Pairing attempted but a token is already stored |
| `unknown_cmd`    | Command name not in registry |
| `bad_params`     | Required param missing or value out of range |
| `busy`           | Device busy (OTA in progress, BLE scan running, etc.) |
| `not_implemented`| Command registered but not yet functional |
| `internal`       | Unexpected internal error; `detail` contains `esp_err` string |
| `frame_too_large`| Received frame exceeds max size |

---

## 4. Command catalog

Commands are dispatched via `CommandHandler::handleExternalCommand()`. The existing `cmd:param` console format is wrapped by the envelope codec; both formats are valid during the migration period.

Commands marked *(portal-bridge)* are called by the captive portal internally and will be removed when [#53 A1] ships. The companion app **must not** use them.

Commands marked *(new)* do not exist in the current firmware and must be added as part of the indicated ticket.

### 4.1 System

| Command           | Params                          | Response `data`                                      | Notes |
|-------------------|---------------------------------|------------------------------------------------------|-------|
| `ping`            | —                               | `{uptime_ms: N}`                                     | Allowed unauthenticated |
| `status`          | —                               | `{wifi:{ssid,ip,rssi}, mqtt:{connected}, ble:{connected_clients:N}, heap_free:N, uptime_ms:N}` | Allowed unauthenticated |
| `help`            | —                               | `{commands: ["name: description", ...]}`             | |
| `reset`           | —                               | *(device reboots; no response sent)*                 | |
| `factoryReset`    | —                               | *(wipes NVS, reboots; no response sent)*             | |
| `listNVSValues`   | —                               | `{wifi0_ssid, deviceName, soundMQTTURL, ...}`        | |
| `clearNVS`        | —                               | —                                                    | |
| `startPortal`     | —                               | —                                                    | |
| `stopPortal`      | —                               | —                                                    | |

### 4.2 Auth / pairing

*(new — added by [#58 A6])*

| Command   | Params               | Response `data` | Auth required | Notes |
|-----------|----------------------|-----------------|---------------|-------|
| `pair`    | `{token: "base64"}`  | —               | no            | If no token stored: stores and returns ok. If token matches stored: returns ok. Otherwise: `unauth` |
| `unpair`  | —                    | —               | yes           | Clears stored token, sets `paired=0` |

### 4.3 Display / navigation

| Command               | Params                     | Response `data`  | Notes |
|-----------------------|----------------------------|------------------|-------|
| `home`                | —                          | —                | |
| `info`                | —                          | —                | |
| `screen`              | `{name: string}`           | —                | Valid names: `info`, `deviceInfo`, `timer`, `light`, `sound`, `temperature`, `pc` |
| `setBrightness`       | `{percent: 0-100}`         | `{percent: N}`   | |
| `setpoint`            | `{value: N}`               | —                | |
| `calibrateOrientation`| —                          | —                | Triggers calibration mode on display |

### 4.4 Device

| Command          | Params           | Response `data`   | Notes |
|------------------|------------------|-------------------|-------|
| `getDeviceName`  | —                | `{name: string}`  | |
| `setDeviceName`  | `{name: string}` | —                 | Persisted to NVS immediately |

### 4.5 WiFi

| Command               | Params                                                  | Response `data`                                      | Notes |
|-----------------------|---------------------------------------------------------|------------------------------------------------------|-------|
| `connectWifi`         | `{ssid, password, slot?: 0}`                            | —                                                    | `slot` is 0-based credential index, default 0 |
| `showNetworks`        | —                                                       | `{networks: [{ssid, slot}]}`                         | Not yet implemented; returns `not_implemented` |
| `configureStaticIP`   | `{ip, gateway, subnet, dns1, dns2}`                     | —                                                    | Restart required |
| `enableStaticIP`      | —                                                       | —                                                    | Restart required |
| `disableStaticIP`     | —                                                       | —                                                    | Restart required; reverts to DHCP |
| `showStaticIP`        | —                                                       | `{enabled: bool, ip, gateway, subnet, dns1, dns2}`   | |

### 4.6 MQTT

| Command                 | Params                                  | Response `data` | Notes |
|-------------------------|-----------------------------------------|-----------------|-------|
| `configureMQTTServer`   | `{host, port: int, username?, password?}` | —             | Configures both sound and light MQTT. Restart required |

### 4.7 Bluetooth

| Command               | Params                           | Response `data`       | Notes |
|-----------------------|----------------------------------|-----------------------|-------|
| `setBluetooth`        | `{enabled: bool}`                | `{enabled: bool}`     | Persisted to NVS |
| `setBluetoothName`    | `{name: string}`                 | —                     | Takes effect after restart |
| `restartBtAdvertising`| —                                | —                     | |
| `clearBtBonds`        | —                                | `{cleared: N}`        | |
| `disconnectBtDevice`  | `{address: "AA:BB:CC:DD:EE:FF"}` | —                     | |
| `forgetBtDevice`      | `{address: "AA:BB:CC:DD:EE:FF"}` | —                     | |
| `startBtScan`         | `{duration_s?: 5}`               | —                     | Max 30 s. Results delivered as `btScanResult` notifications (§5) |

### 4.8 Audio / media

| Command               | Params              | Response `data` | Notes |
|-----------------------|---------------------|-----------------|-------|
| `startSoundRecord`    | `{filename?: string}`| —              | |
| `stopSoundRecord`     | —                   | —               | |
| `playLastSoundRecord` | —                   | —               | |

### 4.9 OTA — URL-based (existing)

| Command     | Params            | Response `data`                              | Notes |
|-------------|-------------------|----------------------------------------------|-------|
| `otaUpdate` | `{url: string}`   | —                                            | Device fetches binary from URL over HTTPS |
| `otaInfo`   | —                 | `{version: string, partition: string}`       | |
| `otaStatus` | —                 | `{busy: bool, last_error: string}`           | |

### 4.10 OTA — binary push over TCP (new — added by [#C3])

Used by the companion app to push a firmware binary directly. **TCP transport only** — BLE throughput is insufficient.

| Command     | Params                                    | Response `data`              | Notes |
|-------------|-------------------------------------------|------------------------------|-------|
| `otaBegin`  | `{size: N, sha256: "hex64"}`              | `{upload_id: string}`        | Allocates OTA partition; returns opaque session ID |
| `otaChunk`  | `{upload_id, offset: N, data: "base64"}`  | `{acked_offset: N}`          | Max chunk 4 096 bytes of raw data (before base64). Chunks may be pipelined; `acked_offset` is the next expected byte |
| `otaAbort`  | `{upload_id: string}`                     | —                            | Cancels and rolls back the OTA partition |

After the final chunk is acknowledged and `acked_offset == size`, the device verifies SHA-256 and reboots. If verification fails the device returns `error: "internal"` with the mismatch detail and does **not** reboot.

### 4.11 Portal-bridge commands *(will be removed in [#53 A1])*

`portalDevice`, `portalWifi`, `portalStaticIpCurrent`, `portalMqtt`, `portalWeather`, `portalTime`, `portalOta`, `portalWifiCredential`

These accept URL-encoded form bodies (the existing portal format). The companion app **must not** use them; use the structured equivalents above.

---

## 5. Notification events

Server-pushed; no `id` field; client sends no response.

| Event           | `data` fields                                               | Trigger |
|-----------------|-------------------------------------------------------------|---------|
| `log`           | `{level: "debug|info|warn|err", tag: string, msg: string}` | Device log messages (when log streaming is enabled via future command) |
| `btScanResult`  | `{address, name, rssi}`                                     | Each BLE device found during `startBtScan` |
| `otaProgress`   | `{upload_id, acked_offset, total}`                          | Emitted every ~4 KiB during `otaChunk` upload |
| `wifiState`     | `{connected: bool, ssid?, ip?, rssi?}`                      | WiFi connect / disconnect |
| `mqttState`     | `{connected: bool}`                                         | MQTT broker connect / disconnect |

---

## 6. Pairing handshake

```
Client                                    Device
  |                                          |
  |-- {"id":1,"cmd":"pair",                  |
  |    "params":{"token":"abc..."}} -------> |
  |                                          | (first pair: stores token)
  |<-- {"id":1,"status":"ok"} --------------|
  |                                          |
  | (all subsequent requests)                |
  |-- {"id":2,"cmd":"status","auth":"abc"}-> |
  |<-- {"id":2,"status":"ok","data":{...}} --|
```

**Token issuance:** generated on device via `POST /api/pair` on the captive portal; displayed for 120 s. 32 random bytes, base64url-encoded (44 chars).

**Re-pair:** sending `pair` with the correct stored token returns `ok` (idempotent).  
**Already-paired guard:** if a token is stored and `pair` is called with a different token, device returns `error: "unauth"` — not `already_paired` — to avoid leaking paired state to unauthenticated callers.  
**Unpair:** `unpair` (authenticated) clears the token; device advertises `paired=0` in mDNS TXT and BLE manufacturer data.

---

## 7. mDNS advertisement

Service: `_qnob._tcp`  
Hostname: `qnob-<last4mac>.local` (e.g. `qnob-a3f1.local`)  
Port: `23456` (matches `CONFIG_QNOB_TCP_PORT`)

**TXT records:**

| Key      | Example value  | Notes |
|----------|----------------|-------|
| `name`   | `Qnob Office`  | NVS device name; prefixed with `(new) ` when `paired=0` |
| `fw`     | `1.4.2-g5fcfca6` | `git describe --tags --always` |
| `mac`    | `a4cf12b3a3f1`  | Full MAC, no colons, lowercase |
| `caps`   | `tcp,ble`      | Available transports |
| `proto`  | `1`            | This spec version; increment on breaking change |
| `paired` | `0` or `1`     | Updated in real time |

---

## 8. BLE advertisement

- Advertising type: `ADV_TYPE_IND`
- Service UUID in AD: `0xABF0` (16-bit)
- Manufacturer data byte 0: `0x01` = paired, `0x00` = unpaired
- Name in scan response: NVS device name (max 29 bytes)
- Interval: 30 ms when `paired=0` and within first 5 min of boot; 40–60 ms otherwise

---

## 9. Versioning

- The `proto` mDNS TXT record and BLE manufacturer data byte 1 carry the integer protocol version.
- **Major version bump** (breaking change): client must refuse to connect if `proto` > its supported version.
- **Minor additions** (new commands, new notification events): no version bump required; clients must ignore unknown fields.
- Current version: **1**.

---

## 10. Implementation notes

- **JSON library:** `cJSON` (already linked via SetupPortal). Use `cJSON_ParseWithLength` for bounded parsing.
- **Envelope size target:** keep typical command envelopes under 200 bytes so a single BLE write suffices (MTU 247 − 3 = 244 bytes payload).
- **Auth enforcement:** gated by `CONFIG_QNOB_REQUIRE_AUTH` (default `y`). Set to `n` in dev builds to skip token checks during [#57 A5] / [#54 A2] bring-up.
- **Thread safety:** `CommandHandler::handleExternalCommand()` is called from the TCP task and the BLE GATT callback — both off the main loop. A mutex must guard the call site (to be added in [#54 A2]).
- **`id` overflow:** `id` is uint32; roll over to 1 after `0xFFFFFFFF` (never use 0).
