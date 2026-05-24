# MVC Refactor — Design Document

Status: **Draft v1** — for review before any code moves.
Scope: Firmware (`homio-round-hmi/src/esp-idf`) + Windows Companion (`homio-round-hmi/Companions/Windows`).

---

## 1. Goals & Non-Goals

**Goals**
- Strict layering: **Transport → Model → Controller → View**, with `InputRouter` as the single hub for all user/external inputs.
- `TargetModel` abstraction so the smart-home target and PC target are interchangeable behind one interface. Adding a third target (e.g. Home Assistant native, Matter) = new module, no controller changes.
- One canonical event vocabulary across touch / knob / voice / network. Adding a new input source = new `InputRouter` subscriber.
- Views are pure renderers, bound to observable models. No views talk to transports, MQTT, BLE, or LVGL-event-to-network logic.
- Firmware and Companion share the same conceptual layout and the same wire protocol (v2).
- **Comprehensive unit-test coverage of the new architectural layers**, developed test-first via TDD. Hardware-adjacent code (transports, HAL, BootGuard, OTA, board init) is excluded from unit testing — covered by manual smoke and on-target integration tests. Details in §9.

**Non-Goals**
- Rewriting transports, HAL, BootGuard, OTA, AMOLED/DMA quirks, mDNS, sleep handler, battery handling, time, weather, captive-portal HTML.
- Preserving wire-level compatibility with current paired clients. Both firmware and Companion ship the new protocol together.
- Performance work. This refactor must not regress, but it isn't an optimization pass.

---

## 2. Target Architecture

```
   ┌───────────────────────────────────────────────────────┐
   │                        View                            │
   │  LVGL screens (firmware) / Qt widgets (companion)      │
   │  Read model state · emit semantic events upstream      │
   └─────────┬────────────────────────────────────┬─────────┘
             │ observes                            │ emits
             ▼                                     ▼
   ┌───────────────────────────────────────────────────────┐
   │                    InputRouter                         │
   │  Single hub. Subscribers: TouchPanel · Knob · Mic VAD  │
   │  · Envelope ingress (TCP/BLE/MQTT) · Portal HTTP       │
   │  Emits semantic events: setpoint_up, select, back,     │
   │  mode_next, mute, target_action(target, action, args)  │
   └────────────────────────┬──────────────────────────────┘
                            │ dispatched by active mode
                            ▼
   ┌───────────────────────────────────────────────────────┐
   │                     Controllers                        │
   │  Per-intent: LightControl · SoundControl · MediaControl│
   │  · SystemControl (mode/brightness/sleep) · PairControl │
   │  · NetControl (wifi/bt/mqtt setup)                     │
   │  No LVGL/Qt. No transport calls. No protocol parsing.  │
   └────────┬────────────────────────────┬──────────────────┘
            │ reads/writes               │ commands
            ▼                            ▼
   ┌────────────────────┐     ┌─────────────────────────────┐
   │   DeviceModel      │     │       TargetModel           │
   │   (observable)     │     │  Abstract. Implementations: │
   │   battery, mode,   │     │  · SmartHomeTarget (MQTT)   │
   │   brightness,      │     │  · PcTarget       (BLE/USB) │
   │   link state, …    │     │  Observable. Reports state. │
   └─────────┬──────────┘     └──────────────┬──────────────┘
             │ state from                    │ frames via
             ▼                                ▼
   ┌───────────────────────────────────────────────────────┐
   │                     Transport                          │
   │  WiFiManager · BluetoothManager · MQTTManager ·        │
   │  Knob serial · Setup portal · (future: USB-serial)     │
   │  KEPT AS-IS. Surface narrowed to: link state events +  │
   │  send/receive byte frames. No command parsing.         │
   └───────────────────────────────────────────────────────┘
```

**Dependency rule (enforced by CMake / Python imports):**
- View depends on: Model (read), InputRouter (write)
- Controller depends on: Model, TargetModel
- Model depends on: Transport (only DeviceModel and Target impls)
- Transport depends on: nothing above it
- **No back-edges.** Catching this at the build layer prevents drift.

---

## 3. Firmware Layout (`src/esp-idf/components/`)

### 3.1 New component tree

ESP-IDF components must stay flat under `components/`. We rename and split, not nest.

```
components/
  ┄┄ kept as-is ┄┄
  hal/                  (was board_hal/)
  display_driver/       (was display/ — LovyanGFX + LCD wrapper only)
  touch_panel/
  knob_serial/          (was knob_controller/ — pure UART now)
  media_io/             (was media/ — mic, speaker, SD, codecs)
  boot_guard/
  ota_manager/
  battery_handler/
  storage/
  time_handler/
  weather_handler/
  sleep_handler/
  internet_handler/
  connectivity_manager/
  mdns_advertiser/
  wifi_manager/
  bluetooth_manager/
  mqtt_manager/
  setup_portal/         (extracted from wifi_manager/)

  ┄┄ new or rewritten ┄┄
  protocol/             (was command_envelope/ — extended to v2)
  model/                NEW: DeviceModel, TargetModel iface, SmartHomeTarget, PcTarget
  controller/           NEW: InputRouter, *Control, ScreenRouter
  view/                 (was ui/ — restructured)
    widgets/            (was Arc, Buttons)
    screens/            (pure renderers)
    LvglDisplay         (LVGL glue stays here)
```

### 3.2 Module disposition

| Old | Disposition | Notes |
|-----|-------------|-------|
| `controllers/SoundController` | **Replace** | Logic → `controller/SoundControl`; widgets → `view/screens/SoundScreen`. State (setpoint) → `DeviceModel` and `TargetModel`. |
| `controllers/LightController` | **Replace** | Same split. Hue/water-level state → `TargetModel.light`. |
| `controllers/MediaController` | **Replace** | Shrinks to `controller/MediaControl`. Audio I/O (Speaker, Mic, SoundPlayer, SoundRecorder) stays in `media_io/`. |
| `ui/screens/ModeController` | **Move** | Becomes `controller/SystemControl` + a small mode-indicator widget under `view/widgets/`. |
| `display/DisplayController` | **Rename + shrink** | → `controller/ScreenRouter`. Job: own current `Mode`, swap views, propagate sleep/brightness events. No screen-internal logic. |
| `command_handler/CommandHandler` | **Delete** | Job splits: envelope decode → `protocol/`, dispatch → `InputRouter`. Per-command handler bodies become controller methods. |
| `command_envelope/` | **Rename + extend** | → `protocol/`. v2 schema (§5). |
| `knob_controller/` | **Rename + shrink** | → `knob_serial/`. Strips UART read loop output to byte frames; semantic interpretation moves to `InputRouter`. |
| `wifi_manager/SetupPortal` | **Extract** | → `setup_portal/`. Portal POSTs become envelope ingress feeding `InputRouter`. |
| `ui/Arc.{h,cpp}`, `ui/Buttons.{h,cpp}` | **Move** | → `view/widgets/`. |
| `ui/screens/*` | **Move + strip** | → `view/screens/*`. Remove `checkTouchInput()`, MQTT callbacks, model-state synthesis. They observe `DeviceModel` / `TargetModel` and emit InputRouter events on user action. |
| `main/main.cpp` | **Major edit** | Slim down. Stays as the wiring layer (it's the right place for instance lifetime). Boot quirks block stays untouched. |
| `display/LGFX_Config.hpp` | **Keep** | Board-specific LCD config. |

### 3.3 Concrete responsibilities

**`model/DeviceModel`**
- Owns: current `Mode`, brightness %, sleep stage, battery telemetry snapshot, wifi/bt/mqtt link state, paired flag, device name.
- Observer pattern: views and controllers subscribe to property change events. **All events delivered through a single FreeRTOS event-bus queue serviced by one dispatcher task.** No consumer ever runs on a producer's stack. View-targeted callbacks marshal through `lv_async_call` from the dispatcher into LVGL's task.
- Backpressure: **drop-oldest** policy. When the bus queue fills, the oldest pending event is discarded to make room for the new one. Acceptable because consumers care about *current* model state; missed intermediate events still leave the system convergent toward the latest values. (See §7 for the unbounded-emitter risk this introduces.)
- Reads from existing handlers (BatteryHandler, ConnectivityManager, WiFiManager, BluetoothManager, MQTTManager, NVSManager). It's the *one* place screens look for these facts. No more callback-per-screen wiring in `main.cpp`.

**`model/TargetModel` (abstract)**
- Interface methods (rough): `setLight(brightness, hue?)`, `getLight()`, `setSoundVolume(pct)`, `getSoundVolume()`, `mediaPlayPause()`, `mediaNext()`, `mediaPrev()`, `setMute(bool)`, plus capability flags (`hasLight`, `hasSound`, `hasMedia`).
- Observable: each property emits change events when the remote state shifts. Controllers reflect this into local UI.
- Concrete impls:
  - `SmartHomeTarget` — talks via `MQTTManager`. Topic taxonomy defined in §5.
  - `PcTarget` — talks via `BluetoothManager` (BLE Nordic-UART today, USB-serial later). Same interface, different wire.
- Only **one** target is "active" at a time, selected by the user via SystemControl. The active target is observable; views can show "controlling: smart home" vs "controlling: PC".

**`controller/InputRouter`**
- Single subscriber to all input sources.
- Translation table: raw input → semantic event, gated by current mode/active screen. Same `setpoint_up` event whether it came from touch arc, knob clockwise turn, BLE envelope `cmd: setpoint`, or MQTT command topic.
- Routes events to the controller subscribed to the relevant intent. Subscriptions are keyed by *intent*, not source. Adding voice = a new subscriber publisher; controllers don't change.

**`controller/*Control`**
- One per intent: Light, Sound, Media, System, Pair, Net.
- Each subscribes to its slice of InputRouter events. Reads `DeviceModel` and the active `TargetModel`. Writes to either. Never touches LVGL or transports.

**`controller/ScreenRouter`**
- Owns the LVGL screen lifecycle. Observes `DeviceModel.mode` and swaps active view. Drives `LvglDisplay::tick()`. Handles brightness/sleep wakeup transitions.

**`view/screens/*`**
- LVGL widgets only. Bind to model events for state (e.g. battery icon redraws when `DeviceModel.battery` changes). On touch interactions, fire `InputRouter::emit(...)`. They do not know about MQTT, BLE, MQTTManager, command names.

### 3.4 What stays untouched in `main.cpp`
- BootGuard begin → emergency check → safe-mode override block.
- AMOLED reset sequence (LCD_RST pin pulses + 150 ms hold).
- PWR_KEY recovery hold detection.
- `gfx->init()` deferred to just before display task starts (the 7-second-gap workaround).
- mDNS start deferred to after SPI DMA claim.
- BT-before-WiFi init order.
- Task creation (`displayTask`, `networkTask`, `commandTask`) and PSRAM stack fallback.

What changes in `main.cpp`: the giant block of `displayController->registerXxx(...)` and `screen->setXxxCallback(...)` calls collapses into a handful of `model->bind(handler)` and `inputRouter->subscribe(controller)` lines.

---

## 4. Companion Layout (`Companions/Windows/src/qnob_companion/`)

### 4.1 New package tree

```
qnob_companion/
  ┄┄ kept ┄┄
  app/                  main.py, settings.py, logging_config.py, updater.py
  transport/            base.py, ble.py, serial.py, tcp.py, client.py
  protocol/             envelope.py (extended to v2)
  pc/                   media.py, windows_audio.py (Windows-specific I/O adapters)
  discovery/            ble.py, mdns.py (low-level scan/announce)
  pairing/              secret_store.py (keyring only)
  ota/                  uploader.py

  ┄┄ new ┄┄
  model/
    device_registry.py   paired devices, per-device link state, capabilities, last-seen
    pc_model.py          local Windows media/audio state (mirrors device's PcTarget peer)
    target_model.py      abstract — mirrors firmware TargetModel
  controller/
    input_router.py      Qt UI signals + inbound envelopes + Windows hotkeys
    device_control.py    sends commands to a paired device, updates registry on responses
    pc_control.py        receives device intents, drives windows_audio/media
    pairing_control.py   was pairing/service.py
    discovery_control.py was discovery/service.py
    connection_control.py was transport/client.py orchestration bits

  ┄┄ moved into view ┄┄
  view/                 was ui/
    main_window.py
    devices_page.py
    device_detail.py
    pairing_wizard.py
    serial_terminal.py
    tabs/
```

### 4.2 Module disposition

| Old | Disposition | Notes |
|-----|-------------|-------|
| `discovery/service.py` | **Move** | → `controller/discovery_control.py`. Discovery primitives (`ble.py`, `mdns.py`) stay where they are. |
| `pairing/service.py` | **Move** | → `controller/pairing_control.py`. `secret_store.py` stays in `pairing/`. |
| `pc/media.py`, `pc/windows_audio.py` | **Keep + invert** | They become drivers called by `controller/pc_control.py`. Today they're called directly from UI in places — that goes away. |
| `transport/client.py` | **Split** | Connection orchestration → `controller/connection_control.py`. The transport classes themselves (`tcp.py`, `ble.py`, `serial.py`, `base.py`) stay as low-level adapters. |
| `ui/` | **Rename to `view/` + strip** | All business logic moves out. UI files emit Qt signals which `input_router.py` subscribes to. UI reads from `device_registry` / `pc_model`. |
| `protocol/envelope.py` | **Extend** | v2 schema. Keep Pydantic models — they're already clean. |

### 4.3 Concrete responsibilities

**`model/device_registry`** — Source of truth for "what devices does this Companion know about?". Per device: id, name, capabilities, link state, last response time, pairing token (proxied to secret_store). Observable for Qt views.

**`model/pc_model`** — The Companion's view of its own Windows machine state: current default audio device, master volume, mute, current media player + transport state (playing/paused). Observable. Updated by `pc_control` from Windows APIs.

**`model/target_model`** — Abstract, mirrors firmware. The Companion is the *peer* to the device's `PcTarget`. Inbound device intents ("PC: volume up") land here as method calls; outbound state changes ("audio device switched") get pushed to the device as notifications.

**`controller/input_router`** — Single hub for: Qt UI signals, inbound device frames (responses + notifications), Windows global hotkeys. Translates each into a semantic intent and dispatches to the relevant `*_control` module.

**`controller/pc_control`** — Receives PC-target intents from a device. Calls `pc/windows_audio.py` and `pc/media.py`. Pushes resulting state changes back to the device via `device_control`.

**`controller/device_control`** — Per-device send/receive logic. Translates user actions into envelopes, parses responses, updates `device_registry`.

---

## 5. Shared Protocol v2

The current [protocol.md](../../Documents/GitHub/homio-screen/homio-round-hmi/docs/protocol.md) v1 is already well-shaped. v2 is additive in spirit but introduces three structural changes to support the new architecture cleanly. Bump `proto=2` in mDNS TXT and BLE manufacturer data.

### 5.1 What changes from v1

**Change A: `target` field on requests.**
The current command catalog is flat: `setBrightness`, `setpoint`, etc. — implicitly target the device or implicitly target whatever MQTT topic the device subscribes to. v2 makes the target explicit:

```json
{ "id": 42, "target": "device", "cmd": "setBrightness", "params": { "percent": 75 } }
{ "id": 43, "target": "smart_home.light", "cmd": "set", "params": { "level": 60 } }
{ "id": 44, "target": "smart_home.sound", "cmd": "setVolume", "params": { "percent": 30 } }
{ "id": 45, "target": "pc", "cmd": "mediaPlayPause" }
```

Targets enumerated in §5.3. Maps directly to the device's `TargetModel` instances. Companion sending `target: "pc"` to a device means "the device's PC peer (this Companion) should do X."

**Change B: State subscriptions (push, not poll).**
v1 has `notification` for a fixed event set (`wifiState`, `mqttState`, `log`, etc.). v2 generalizes:

```json
// client subscribes
{ "id": 50, "cmd": "subscribe", "params": { "paths": ["device.battery", "smart_home.light", "pc.audio.volume"] } }

// device pushes
{ "type": "state", "path": "device.battery", "data": { "percent": 73, "charging": false } }
{ "type": "state", "path": "smart_home.light", "data": { "level": 60, "on": true } }
```

Replaces the ad-hoc per-fact notifications. Same wire shape covers any model property.

**Change C: Semantic input events alongside commands.**
For the bidirectional input router story, v2 carries a small set of *intent* events that map directly onto InputRouter's vocabulary:

```json
{ "id": 60, "target": "device", "cmd": "intent", "params": { "action": "setpoint_up" } }
{ "id": 61, "target": "device", "cmd": "intent", "params": { "action": "mode_next" } }
```

Lets external clients drive the same code paths as touch/knob. (Voice and a hypothetical mobile remote would use this.)

### 5.2 What stays from v1
- Envelope shape (`id`, `cmd`, `params`, `auth`, plus the new `target`).
- Response shape (`id`, `status`, `data`/`error`/`detail`).
- Error codes (§3 of protocol.md).
- Transport framing (TCP newline-delimited, BLE length-prefixed).
- Pairing handshake.
- mDNS TXT structure.
- BLE advertisement.
- OTA binary push (§4.10) — bolts onto the new architecture unchanged.

### 5.3 Target taxonomy

| `target` | Lives on | Owned by | Notes |
|----------|----------|----------|-------|
| `device` | the device itself | `DeviceModel` | All device-self facts: brightness, mode, sleep, battery, pairing. |
| `smart_home.light` | remote (broker) | `SmartHomeTarget.light` | MQTT-backed in current setup. |
| `smart_home.sound` | remote (broker) | `SmartHomeTarget.sound` | MQTT-backed. |
| `smart_home.temperature` | remote (broker) | `SmartHomeTarget.temperature` | Read-only on the device side; future writable. |
| `pc` | Companion | `PcTarget` on device ↔ Companion's `pc_control` | BLE/USB-serial wire. Subdomains: `pc.audio`, `pc.media`, `pc.input` (future). |

Capability discovery: `cmd: capabilities, target: "device"` returns the list of targets this device exposes, with available subdomains per target. Companion uses this to grey out controls when a target isn't supported.

### 5.4 Command catalog migration

Per-target catalogs replace v1's flat list. Examples:

- `target: "device"` keeps: `ping`, `status`, `help`, `reset`, `factoryReset`, `setBrightness`, `screen`, `intent`, `subscribe`, `capabilities`, `getDeviceName`, `setDeviceName`, `connectWifi`, `configureStaticIP`, …
- `target: "smart_home.light"`: `get`, `set`, `setHue`, `setMode`.
- `target: "smart_home.sound"`: `get`, `setVolume`, `play`, `pause`, `next`, `prev`.
- `target: "pc"`: `mediaPlayPause`, `mediaNext`, `mediaPrev`, `volumeUp`, `volumeDown`, `setMute`.

Portal-bridge commands (§4.11 in v1) are deleted, not migrated. The captive portal posts directly into `InputRouter` via local HTTP — no envelope needed for portal traffic.

---

## 6. Migration Slices

Six slices. After each, firmware boots, the device functions, and the previous behavior of that slice is preserved (against the *new* protocol).

**TDD discipline applies to every slice:** for each new class introduced in a slice (Model, Controller, InputRouter component, codec, Companion controller/model), tests are written **before** implementation (red → green → refactor). Every slice exit criterion implicitly includes "all unit tests pass on host" and "pre-existing tests still pass." The specific test surface per slice tracks the layers that slice introduces — see §9 for the framework details.

### Slice 0 — Scaffolding (no behavior change)
- Create empty `model/`, `controller/`, `view/` components with CMakeLists.
- Create `view/widgets/`, `view/screens/` subdirs.
- Create `protocol/` (rename from `command_envelope/`), add v2 schema types (`Envelope`, `Response`, `Notification`, `StatePush`).
- Companion: create `model/`, `controller/`, `view/` packages; move `pairing/service.py` and `discovery/service.py` and `transport/client.py` orchestration parts. Rename `ui/` → `view/`.
- **Set up host-side test infrastructure for firmware:** CMake host target, Unity + CMock, minimal stub set for the FreeRTOS / ESP-IDF APIs we'll actually need (queues, semaphores, esp_timer, esp_log). One trivial passing test proves the harness works.
- **Implement `TestEventBus`** — synchronous, manually-pumped shim that controllers and models use under test instead of the production async dispatcher.
- **Companion test scaffolding:** confirm existing pytest setup runs green; add `tests/controller/` and `tests/model/` directories for the new layers.
- **CI:** GitHub Actions workflow running host unit tests + pytest on every PR. Build green on both sides. No functional change yet.

### Slice 1 — Foundation
- Implement `model/DeviceModel` (observable). Wire it to existing handlers (battery, wifi, mqtt, bt, nvs).
- Implement `model/TargetModel` interface + empty `SmartHomeTarget`, `PcTarget` stubs.
- Implement `controller/InputRouter` skeleton — subscribes to TouchPanel and KnobController; emits semantic events to a console log (no consumers yet).
- Implement `controller/ScreenRouter` — wraps current `DisplayController` logic, but reads mode from `DeviceModel` and ignores knob direct (knob events now flow through `InputRouter`).
- Companion: implement `model/device_registry`, `model/pc_model`, `controller/input_router`. Hook Qt signals through it (still calling existing transport directly for now).
- **Exit criteria:** All existing screens still display correctly. Touch and knob still work. No new architecture in active use yet — these are skeletons.

### Slice 2 — Sound (firmware vertical)
- New `view/screens/SoundScreen` (pure renderer reading `TargetModel.sound`).
- New `controller/SoundControl` (handles `setpoint_up`/`setpoint_down`/`play`/`pause`/`back` intents).
- New `SmartHomeTarget.sound` (talks MQTT, emits state changes).
- Wire: Touch → InputRouter → SoundControl → TargetModel/DeviceModel → SoundScreen redraws.
- Delete old `controllers/SoundController`.
- **Exit criteria:** Sound screen drives MQTT-backed audio with new protocol path. Companion's existing tabs unaffected.

### Slice 3 — Light (firmware vertical)
- Same pattern as Sound: `LightScreen`, `LightControl`, `SmartHomeTarget.light`.
- Delete old `controllers/LightController`.
- **Exit criteria:** Light screen works through new architecture.

### Slice 4 — PC target end-to-end (both sides)
- Firmware: new `controller/MediaControl`, fill in `PcTarget` (BLE Nordic-UART send via `BluetoothManager`).
- Firmware: replace `PlaceholderPageScreen`'s play/pause hook with proper `MediaControl` events.
- Companion: implement `controller/pc_control` driving `pc/windows_audio.py` and `pc/media.py`.
- Companion: implement `controller/device_control` for outbound envelope sending.
- Update Companion `protocol/envelope.py` for v2 (target field, state push). Bump `proto=2` in mDNS check.
- **Exit criteria:** PC volume / play-pause via the knob+screen drives Windows correctly. Old BLE Nordic-UART commands are deleted from firmware; Companion only speaks v2.

### Slice 5 — System, networking, pairing (firmware)
- New `controller/SystemControl` covers everything previously split across `ModeController`, `DisplayController.setBrightness`, sleep wakeup, mode transitions.
- New `controller/PairControl`, `controller/NetControl` absorb the corresponding `CommandHandler` cases (`pair`, `unpair`, `connectWifi`, `configureMQTTServer`, etc.).
- DeviceInfoScreen and EnvironmentInfoScreen become pure views.
- Setup portal POSTs are re-routed: portal calls into `InputRouter` directly, not via `CommandHandler`.
- **Delete** `CommandHandler`, `DisplayController`, `ModeController`, `controllers/` directory entirely.
- Slim down `main.cpp`.
- **Exit criteria:** All current device features work through new architecture. Portal works. `CommandHandler` gone.

### Slice 6 — Cleanup + docs
- Update `docs/protocol.md` to reflect v2 fully.
- Add a top-level architecture doc to the repo.
- Remove now-unused command-handler code paths, legacy `cmd:param` parser, portal-bridge command handlers.
- Run a sweep for unused fields in NVS keys, dead screens, vestigial callbacks.

---

## 7. Risk Register

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Boot ordering changes accidentally during `main.cpp` slimming, breaking AMOLED / mDNS / BT-coex | Medium | High (blank screen, comms broken) | The boot quirks block is fenced off at the top of `app_main`. Slice 5 must not edit anything before the `Initialization complete` log line. Add a comment fence in code. |
| Knob events currently flow through `CommandHandler::handleKnobSetpoint`; the new path is `InputRouter`. Edge cases (rapid turn, debounce) may regress. | Medium | Medium | Slice 1 ports knob into `InputRouter` first while old path still exists. Cross-verify both paths emit equivalent events for the same UART stream before deleting the old path in slice 5. |
| Event-bus saturation. High-frequency producers (battery telemetry, RSSI) can flood the single bus queue; drop-oldest then evicts *unrelated* events (mode change, pair complete) as collateral damage. | High | Medium | Cap producer rates at the source: BatteryHandler ≤ 2 Hz, RSSI only on significant Δ, log emitter rate-limited. Audit every producer for emission rate during slice 1. Drop-oldest is the backstop; producer discipline is the primary mitigation. |
| MQTT subscription topology change breaks third-party Home Assistant integrations users may have built. | High | Medium | Document v2 topic taxonomy in `docs/protocol.md` clearly. Provide migration notes. Acceptable per "free to redesign". |
| Companion app shipping in lockstep — if firmware updates but Companion doesn't, users lose PC control until Companion update lands. | High | Medium | Ship Companion update concurrently. Companion checks `proto=2` in mDNS and refuses connection to old firmware with a clear message ("update firmware"). Ditto firmware refuses `proto=1` clients ("update Companion"). |
| BootGuard escalation depends on a 15-second healthy-boot timer in `armHealthTimer()`. Refactor might delay reaching that line. | Low | High (boot loop into safe mode) | Keep the `armHealthTimer()` call at the same point in `app_main`. If slice 5 reorders init, measure boot time before/after. |
| LVGL is single-threaded. Views observing models from non-LVGL tasks could race. | Low | High (rare crashes) | Mitigated by design: all model events go through the single dispatcher task, which marshals view callbacks via `lv_async_call`. With the all-queued event bus (§3.3), no view callback ever runs on a producer's task. Risk reduces to LVGL bugs in `lv_async_call` itself. |
| Setup portal HTML hardcodes existing command names. | High | Low (portal stops working until updated) | Slice 5 updates portal POST handlers and HTML form actions together. Smoke-test all portal pages after slice 5. |
| The split between Model (state) and Target (remote) becomes blurry for properties like "indoor temperature" which are remote facts but feel device-y. | Medium | Low | Default rule: if the device can act on it locally without a network, it's `DeviceModel`. If it requires another party to read or write, it's a `Target`. Indoor temp = `smart_home.temperature`. |
| OTA partition slot semantics depend on `OTAManager` which stays. New `controller/SystemControl.startOta` must call the manager correctly. | Low | High (brick risk) | Slice 5 must include a smoke-test: OTA download → verify → reboot → confirm. |

---

## 8. Resolved Decisions

These were open at draft v1 and are now locked. Recorded for traceability.

| # | Decision | Resolution |
|---|----------|------------|
| 1 | ESP-IDF component granularity | **Single component per layer** (`model/`, `controller/`, `view/`). Internal organization via subdirectories; files inside are translation units, not nested components. |
| 2 | Model change event delivery | **All queued.** Single FreeRTOS event-bus queue, one dispatcher task, drop-oldest backpressure. No consumer runs on a producer's stack. Latency cost: ~10–30 ms knob-to-pixel under load. Win: full one-directional dataflow, deterministic ordering, no priority inversion. Implementation per §3.3. |
| 3 | Voice command entry point | A future wake-word/VAD module emits semantic intents into `InputRouter`. Slot present in v2 protocol as `cmd: intent`. Not implemented in this refactor — the architecture just leaves the door open. |
| 4 | v2 notifications vs state-push | **State-shaped events become subscribable paths** (`device.wifi`, `device.mqtt`, etc.). **Stream-shaped events** (`log`, `otaProgress`, `btScanResult`) stay as v1-style one-way notifications. Both wire forms coexist by design — stateful vs. streamy is a real distinction in the protocol. |
| 5 | Captive portal HTML location | Stays in `setup_portal/` component (which owns its HTTP server). Portal POST handlers call `InputRouter::emit(...)` directly — no envelope round-trip for in-process traffic. |
| 6 | Companion process split (UI ↔ service) | **Out of scope.** Single process for this refactor. Revisit after slice 6. |
| 7 | Branch strategy | **Long-lived `refactor/mvc` branch.** Slice-sized PRs merge into the branch; one squash merge to `main` after slice 6. Mainline stays open for hotfixes during the refactor. |
| 8 | Testing approach | **Strict TDD on new architectural layers.** Coverage scope: Model, Controllers, InputRouter, protocol Codec, Companion model/controller (full). Hardware-adjacent code excluded from unit tests. Unit tests run host-only (fast inner loop); on-target integration tests run periodically (manual or CI when hardware connected). Frameworks: Unity + CMock (firmware), pytest + unittest.mock (Companion). Async event bus tested via manually-pumped `TestEventBus`. Details in §9. |

---

## 9. Testing Strategy

### 9.1 Scope

| Layer | Unit-tested? | Notes |
|-------|--------------|-------|
| `model/DeviceModel`, `TargetModel` interface, `SmartHomeTarget`, `PcTarget` | **Yes** | Pure logic + mock transport. Drive with TDD. |
| `controller/InputRouter`, `*Control` (Light, Sound, Media, System, Pair, Net) | **Yes** | Pure logic + mock model/target. Drive with TDD. |
| `controller/ScreenRouter` | **Yes** | LVGL mocked. Tests focus on mode-transition logic, not rendering. |
| `protocol/` (envelope codec) | **Yes** | Parse/serialize round-trip, error paths, frame-size limits. |
| Companion `model/`, `controller/` | **Yes** | pytest. Mock transports. |
| `view/screens/*` (LVGL) | **No** | Visual; manual smoke. A handful of tests on widget *state* (e.g. arc setpoint→pixel position) where pure-logic. |
| Transports (`wifi_manager`, `bluetooth_manager`, `mqtt_manager`, `setup_portal`, `knob_serial`) | **No** | Hardware/network dependent. Integration-tested on target. |
| HAL, BootGuard, OTAManager, board init | **No** | Hardware/NVS state machine. Manual smoke + on-target. |
| Companion `view/` (Qt), `pc/windows_audio.py` | **No** | Qt visual + Windows API. Manual smoke. |

Coverage target on the **Yes** rows: ≥90% branch coverage. Anything uncovered must be commented "deliberately untested: <reason>" (e.g. defensive checks for impossible states). Coverage tracked via `gcov`/`lcov` for firmware host tests, `coverage.py` for Companion. Reported in CI.

### 9.2 Discipline

Strict TDD per task within a slice:

1. **Red** — write a failing test describing the desired behavior. Run it; confirm it fails for the right reason.
2. **Green** — write the minimum implementation that makes the test pass. Resist over-engineering.
3. **Refactor** — clean up implementation *and* test. Tests are production code; bad tests rot.

Tests merge in the same PR as the implementation they cover. PR review confirms tests exercise the behavior, not just lines (i.e. assert observable outcomes, not internal calls). No feature is "done" until its tests are reviewed.

### 9.3 Execution

**Inner loop (every save / commit):**
- Firmware host tests: CMake host target, runs in <2 s for the full unit suite at slice 1's size; <30 s by slice 6.
- Companion pytest: <5 s.
- Both run locally without hardware.

**CI (every PR):**
- Same host tests + pytest, GitHub Actions runner.
- Coverage report posted.

**Periodic integration (on-target, manual or scheduled):**
- Real ESP32 device flashed with branch firmware.
- End-to-end tests: pair → control sound via MQTT → drive PC media via BLE → OTA round-trip.
- Run before merging each slice PR to the long-lived refactor branch, and once more before the final main-merge.

### 9.4 Frameworks

| Project | Test framework | Mocking | Coverage |
|---------|---------------|---------|----------|
| Firmware (host) | Unity (ESP-IDF default) | CMock | gcov + lcov |
| Firmware (target) | Unity (ESP-IDF unity component) | Manual / hand-rolled stubs | n/a |
| Companion | pytest | pytest-mock, unittest.mock | coverage.py |

Why Unity over Catch2/GoogleTest: it's the ESP-IDF default, requires no extra dependencies, and the team is most likely to find existing learning material aligned to it. Switching cost later is low if we change our minds.

### 9.5 The `TestEventBus` shim

Production code uses an async event bus (§3.3). Tests cannot afford that asynchrony — they need deterministic, race-free assertions. The shim:

```
// production
EventBus bus;                       // async dispatcher, FreeRTOS queue
bus.emit(Event::Light::SetpointChanged{.value = 60});
// returns immediately; consumers are notified on dispatcher's next tick.

// tests
TestEventBus bus;                   // sync, manually pumped
bus.emit(Event::Light::SetpointChanged{.value = 60});
// queued, no dispatch yet.
bus.pump();                          // synchronously dispatches all pending.
ASSERT_EQ(target.lightLevel, 60);
```

Same interface; tests substitute the bus at construction. No `sleep()` calls in tests; no flakiness from FreeRTOS scheduling jitter.

### 9.6 Test taxonomy per slice

Each slice's tests track its new layers:

- **Slice 0:** the harness itself + `TestEventBus`.
- **Slice 1:** `DeviceModel`, `TargetModel` interface, `InputRouter` skeleton.
- **Slice 2:** `SoundControl`, `SmartHomeTarget.sound`. Mock MQTT transport via `MockMqttPublisher`.
- **Slice 3:** `LightControl`, `SmartHomeTarget.light`.
- **Slice 4:** `MediaControl`, `PcTarget`. Companion: `pc_control`, `device_control`. Cross-process tests use the same envelope on both sides.
- **Slice 5:** `SystemControl`, `PairControl`, `NetControl`, `ScreenRouter`. Setup-portal handlers tested at HTTP boundary.
- **Slice 6:** Add tests for any edge cases surfaced during slices 1–5 but deferred. Lock the coverage targets.

### 9.7 What is NOT tested (explicit list)

- LVGL widget rendering (visual).
- BLE pairing on real hardware (race-prone; manual).
- AMOLED display init timing (timing-sensitive; smoke only).
- WiFi captive portal HTML rendering (browser-side).
- MQTT broker connection retries (network dependent; integration).
- OTA actual flash write (destructive; manual).

These are listed so reviewers don't ask "why no test for X" — the absence is by design, not by oversight.

---

## 10. What this gets you

- Adding a new target (e.g. Home Assistant native, Matter, a second smart-home broker): write a new `TargetModel` impl + tests. Zero controller or view changes.
- Adding a new input (voice, mobile app, gesture sensor): new `InputRouter` publisher + tests. Zero controller changes.
- Adding a new screen: new `view/screens/X`, wire it to existing model events. No transport touch.
- Testing controllers without hardware: instantiate `DeviceModel` and a mock `TargetModel`, feed `InputRouter` events via `TestEventBus`, assert state. No LVGL, no MQTT broker required.
- Companion gets the same separation for free — testable Qt-less PC-control logic.
- Regression safety: by slice 6, ≥90% branch coverage on the new architecture. A future change that breaks expected behavior surfaces in CI, not in the field.

## 11. What this does NOT get you (be honest)

- Faster boot.
- Smaller binary.
- More features per se. The refactor's value is *future* speed of adding features.
- A clean fix for any current bug. Existing bugs migrate forward; surface them as bug-fix PRs against new architecture.
- 100% coverage. Hardware-adjacent paths (transports, HAL, BootGuard, OTA, board init, LVGL views) are intentionally excluded from unit testing. Test ROI on those is poor; they're covered by manual smoke and periodic on-target integration.
