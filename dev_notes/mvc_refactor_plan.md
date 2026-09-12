# MVC Refactor Plan

## Goal

Move the firmware from fat screen classes to a layered MVC model:

- `model` owns all domain state and domain rules. No LVGL, no drivers, no transport.
- `view` owns LVGL widgets only. It renders a model, and does nothing else.
- `controller` owns input interpretation, timing, model mutation, and navigation.
- Transport (MQTT/UART) and sensor polling push into the model through adapters.
- Component dependencies point one direction only.

## Why

Today every screen class is all three layers at once. `SoundController` holds the
setpoint (model), the `lv_obj_t*` widget tree and `drawSetPoint()` (view), and
`checkTouchInput()` + `sendSetpointToServer()` + `processIncomingMessage()`
(controller + transport). `LightController` and `EnvironmentInfoScreen` have the
same shape. There is no model layer at all: screens pull data through
`std::function` callbacks wired in `main.cpp`.

## Defects This Fixes

These are concrete, not stylistic:

1. **Circular component dependencies.** `controllers` REQUIRES `display` and `ui`;
   `ui` REQUIRES `display`; `display` PRIV_REQUIRES `controllers` and `ui`. The
   cycle only survives because one edge is private.
2. **Two different types named `Mode`.** `DisplayController.h` declares a global
   `enum Mode { INITIALIZATION, INFO, DEVICE_INFO, CALIBRATE_ORIENTATION, SOUND,
   LIGHT, HOME, SLEEP }`. `ModeController.h` declares `ModeController::Mode
   { MODE_LIGHT, MODE_TEMPERATURE, MODE_SOUND, MODE_COUNT }`.
   `DisplayController::update()` hand-translates between them. Two same-named
   enums in one program is a bug magnet.
3. **Cross-task model access with no synchronisation.** On ESP32-C6
   `DUAL_CORE_AVAILABLE` is 0 and both `DISPLAY_CORE` and `NETWORK_CORE` are 0, so
   displayTask / networkTask / commandTask time-slice one core. `networkTask`
   writes WiFi/MQTT/weather state while `displayTask` reads it through the status
   callbacks. This race exists today; MVC makes it explicit and fixable in one place.
4. **21 touch-polling sites across 5 classes.** `ModeController` 8,
   `LightController` 5, `EnvironmentInfoScreen` 3, `DeviceInfoScreen` 3,
   `SoundController` 2. Each re-implements press/hold/gesture interpretation.
5. **Flag-polling navigation.** `isPageBackRequested()` / `resetPageBackRequest()`
   on five classes, drained by a switch in `DisplayController::update()`.
6. **`main.cpp` is the message bus.** 559 lines of wiring, including MQTT routing
   by substring match: `topic.find("sound")`, `topic.find("light")`.
7. **Dead code.** `DisplayController::transitionToMode()` is a switch whose every
   branch is empty. `CALIBRATE_ORIENTATION` is never entered.
   `InitializationScreen::setWiFiStatus()` / `setMQTTStatus()` are never called.

## Target Layering

Dependency arrows point up only.

```
        app / composition root
                  ^
             controllers          (model + view + input + nav)
                  ^
               view / ui          (model + lvgl + display backend)
                  ^
                model             (stdlib only - host-testable)
                  ^
  drivers: board_hal, touch_panel, knob_controller,
           display backends, esp_lcd_*, media, storage,
           wifi_manager, mqtt_manager, internet_handler
```

Rule: `model` REQUIRES nothing project-local. `display` loses its
`PRIV_REQUIRES controllers ui sleep_handler` - that edge is the cycle.

## Model Layer (new component)

`components/model/` - aggregate root plus per-domain models:

| Model | Holds |
|---|---|
| `MediaModel` | activeTarget, volume, isPlaying, track, position/duration, knobBinding - see Integrations |
| `LightModel` | brightness, hue, ledMode |
| `ClimateModel` | targetTemp, plus humidity/pressure if the BME688 is read locally - see Integrations |
| `EnvironmentModel` | date, time, dayOfWeek, indoorTemp, outdoorTemp |
| `ConnectivityModel` | wifiConnected, signalStrength, internetAvailable, mqttConnected |
| `PowerModel` | batteryPercent, voltage, connected, presenceKnown |
| `NavigationModel` | activeScreen, selectedMode |
| `KnobModel` | detentCount, haptic config (poles, detentStrength, snapPoint, voltageLimit), link liveness |

**"Setpoint" is three different values sharing one name.** Fix the naming as part
of the split, because the shared name is what hides the drift:

- Knob `setPoint` (`HapticController3.ino:14`) is a bare detent counter - net
  detents since boot, clamped 0-100 by `FOC.ino:96,99`. No physical meaning.
  Becomes `KnobModel::detentCount`. (`last_setPoint` is assigned at `FOC.ino:138`
  and never read - delete it.)
- `SoundController::setpoint` is the real volume. Becomes `MediaModel::volume`.
- `LightController::setpoint` is dead. It inits to 50 and is read once, at line
  139, dirty-tracking a value nothing mutates. The live value is `waterLevel` -
  named for the visual fill metaphor, which is a view concern. Becomes
  `LightModel::brightness`; delete `setpoint`.

Also normalise two inconsistencies here: the knob sends `setpoint=N` while MQTT
sends `setpoint:N`, parsed in different places; and the screen holds 0-100
internally but publishes light brightness as 0-255 via `(waterLevel * 255) / 100`.

**The knob is a model, not just an input source.** The V11 board is a peer device
holding replicated state, not a keyboard: it owns an authoritative setpoint 0-100
(clamped on the C3), has settable haptic config, and the UART protocol is
bidirectional by design (`sp?` query, `setpoint=` write). Its presence also varies
by board - `KNOB_TX_PIN`/`KNOB_RX_PIN` are -1 on the S3 AMOLED 1.75 variant.

The split is both layers, not either:

- A detent tick (`+` / `-`) is a transient event. Stays in `InputRouter` as `InputEvent`.
- Setpoint, haptic config, and link liveness are replicated state. They go in `KnobModel`.

`KnobModel` must define a reconciliation policy, because two defects today have no
place to live without one:

1. **The screen never writes to the knob.** `KnobController::askSetpoint()` and
   `sendCommand()` have zero call sites outside the driver. The entire write side
   is implemented on both ends and unused, so the two setpoints can only drift,
   never resync.
2. **The knob setpoint is mode-agnostic; the screen's is per-mode.** The knob keeps
   one 0-100 counter while the screen keeps separate Sound and Light setpoints, and
   nothing resyncs on mode switch. `FOC.ino handleIncrementDecrement()` sends
   absolute `setpoint=N` instead of `+`/`-` when detents accumulate faster than one
   per cycle; that lands in `commands["setpoint"]` to `cmdKnobSetpoint` to
   `displayController->setSetpoint(N)` and hits whichever mode is active. Switch
   Sound to Light, spin fast, and a Sound-era counter is written into Light
   brightness.

Ownership also needs settling: `+`/`-` is consumed twice today - by
`DisplayController::update()` per-mode and by `CommandHandler`'s registered
`commands["+"]`/`commands["-"]` - from displayTask and commandTask, off the same
`getReceivedCommand()`. After the refactor `KnobModel` is the single owner and
`ScreenRouter` reads mode intent from it.

Change notification via a small fixed-capacity `Observable<T>`. Subscriptions are
wired once at startup. No heap allocation in the render path - see Constraints.

Domain logic moves out of the views: `hsvToRgb()` (from `LightController`),
`formatDate()` / `getMonthName()` (from `EnvironmentInfoScreen`), setpoint
debounce and rate-limit timers (from Sound/Light).

**Colour mapping splits.** `getTemperatureColor()`, `getBatteryColor()` and
`getWifiStrengthBars()` return presentation types. The model exposes
`TemperatureBand` / `BatteryBand` / `SignalBars` enums; the view maps band to
`lv_color_t`. Thresholds are domain, colours are view.

## View Layer

`components/ui/` reorganised. `IScreenView` with `build()`, `render(const XModel&)`,
`destroy()`, `root()`. Views own `lv_obj_t*` handles, `scalePx()`, and LVGL
animations. Views hold no mutable domain state, poll no input, and touch no
transport.

`SoundView`, `LightView`, `EnvironmentView`, `DeviceInfoView`,
`InitializationView`, `ModeSelectView`.

## Controller Layer

`ScreenController` base with `onEnter()`, `onExit()`, `onInput(const InputEvent&)`,
`tick(nowMs)`. One controller per screen. Each owns input interpretation, timing,
model mutation, and navigation requests.

`ScreenRouter` replaces `DisplayController`'s mode machine - owns the screen
state, consumes `NavigationRequest` events, drives onExit/onEnter.

## Adapters

- `InputRouter` - sole owner of `TouchPanel` + `KnobController`. Emits a unified
  `InputEvent { type, x, y, gesture, durationMs, knobDelta }` to the active
  controller and the activity tracker. Replaces all 21 polling sites and the
  input block at the top of `DisplayController::update()`.
- `MqttModelBridge` - owns every topic in `MQTTManager.h` (`esp32/sound/*`,
  `esp32/light/*`, `esp32/command*`, `qnob/indoor/temperature`). Model change to
  publish; inbound payload parsed to model set. Deletes the substring router in
  `main.cpp`.
- `IMediaTarget` with `PcMediaAdapter` and `SpotifyAdapter` - see Integrations.
- `ConnectivityPoller`, `EnvironmentPoller`, `BatteryPoller` - push into the model
  on a timer in networkTask. These replace the pull callbacks
  (`setNetworkStatusCallback`, `setBatteryCallback`, `setDateTimeCallback`,
  `setOutdoorTempCallback`).
- `AppComposition` - the composition root, extracted from `main.cpp`.

## Integrations: Spotify, YouTube, Climate - decided 2026-09-12

Decisions:

- Spotify: **direct**, on-device via the Spotify Web API.
- YouTube: **PC-mediated** - play, pause, rewind, forward, and absolute seek
  with the knob (knob position 0-100 maps to 0-100% of the video).
- Mode wheel: **LIGHT / CLIMATE / MEDIA**. Target selection lives inside MEDIA.
- Matter: **dropped for now.** `partitions.csv` keeps the esp-matter factory
  layout and `sdkconfig.defaults.esp32c6` keeps its "Matter over WiFi" block;
  both are inert without the esp-matter component and can stay.

Spotify and the PC are two adapters behind one interface. Neither is a screen.
The only new models are `MediaModel` (replacing `SoundModel`) and `ClimateModel`.

### MediaModel

| Field | Source |
|---|---|
| `activeTarget` | `SPOTIFY` or `PC` - chosen inside the MEDIA screen |
| `volume` 0-100 | knob, or state push from the active adapter |
| `isPlaying` | state push |
| `track` title / artist / album | Spotify: API. PC: window title today, extension later |
| `positionMs`, `durationMs` | Spotify: API. PC: only with the browser extension |
| `targetLabel` | PC: the player the PC app detected - `get_current_player()` already exists |
| `knobBinding` | `VOLUME` or `SEEK` - what the knob currently drives |

### IMediaTarget - `components/media_target/`

```
setVolume(percent)
togglePlayPause()  play()  pause()
next()  previous()
seekTo(percent)          absolute - the knob-scrub case
seekBy(deltaMs)          relative
requestState()           adapter pushes the result into MediaModel
isAvailable()
```

`MediaScreenController` owns the knob-binding state machine: knob drives volume
by default; a gesture switches it to seek. In seek, detent deltas accumulate into
a 0-100 position and one `seekTo()` fires on release or after a short idle. The
controller calls the interface and never names a target.

### SpotifyAdapter - `components/spotify_client/`

- Web API over HTTPS: `PUT /v1/me/player/play|pause|volume|seek`,
  `POST /v1/me/player/next|previous`, `GET /v1/me/player` for state.
- **Playback control requires Spotify Premium.** Free accounts get 403 on every
  `PUT`/`POST` above. Non-negotiable on Spotify's side.
- Cert bundle is already full (`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL`);
  no cert work.
- **Provisioning goes through the PC app.** OAuth PKCE needs a browser and a
  loopback redirect (`http://127.0.0.1:PORT/callback` - Spotify allows loopback
  IP only, not `localhost`), which the desktop app has and the round screen does
  not. The PC app completes the flow and sends client ID + refresh token to the
  device with a new `CommandHandler` command
  (`configureSpotify:<clientId>:<refreshToken>`), same pattern as
  `configureMQTTServer`. Device stores both in NVS.
- Device refreshes access tokens itself (`POST accounts.spotify.com/api/token`,
  `grant_type=refresh_token`). **PKCE refresh tokens rotate** - persist the new
  one on every refresh or the device locks itself out.
- Send the refresh token over MQTTs or the serial path, never plaintext MQTT.
- Poll `GET /v1/me/player` only while the MEDIA screen is active and the target
  is Spotify; back off on 429.
- Heap: one more TLS session alongside MQTT-TLS. Measure after Phase 0.

### PcMediaAdapter - `components/media_target/`

The MQTT code currently inside `SoundController`, extracted. Speaks the existing
`esp32/sound/control` / `esp32/sound/response` protocol.

**Absolute seek is the gap.** The PC app controls YouTube with global media keys
(`VK_MEDIA_PLAY_PAUSE / NEXT / PREV`), and its `forward` / `rewind` commands map
to `next_track` / `prev_track` - track skip, not seek. Media keys cannot seek.
Options for `seekTo()` on the PC side:

1. **Browser extension** (recommended). Content script on youtube.com exposes
   `player.seekTo()`, `getCurrentTime()`, `getDuration()`, `getPlayerState()`
   and the title over a local WebSocket to the PC app. Precise, works when the
   tab is not focused, and replaces window-title heuristics with real state.
   New code under `PC/`.
2. **Digit keys** as the interim. YouTube maps `0`-`9` to 0%-90%. 10% steps,
   and `keybd_event` digits only reach the focused window - unlike media keys,
   which are global.

Either way the PC-side protocol grows: `seek_abs:<percent>`, `seek_rel:<ms>`,
and a richer state reply (`position`, `duration`, `title`, `player`).

### ClimateModel and the CLIMATE screen

The wheel's TEMPERATURE entry is dead today (`// Temperature mode not yet
implemented` in `DisplayController`). CLIMATE is a new screen, not a rename.

- `ClimateModel`: targetTemp, plus humidity/pressure if the BME688 on
  `ScreenBase_V2` is read locally. Today indoor temperature arrives on
  `qnob/indoor/temperature` over MQTT.
- Transport is MQTT via `MqttModelBridge`.
- `EnvironmentModel` keeps date/time/weather and the two temperatures for the
  INFO screen. Decide whether the temperatures move into `ClimateModel` when the
  CLIMATE screen is designed - not before.

### Naming collision

Existing `MediaController` (`components/controllers/`) is local audio: SD card
sound effects, mic recording, touch SFX. Once `MediaModel` means remote playback
the name is wrong. Rename to `LocalAudio` during Phase 3.

## Phases

Every phase ends buildable and flashable. Do not batch them.

### Phase 0 - Baseline

- Commit the current tree. 595 tracked deletions and the whole `screen/SW/ESP/`
  reorg are uncommitted; there is no rollback point today.
- Record `idf.py set-target esp32c6 && idf.py build` binary size and free heap at boot.
- Tag it.

### Phase 1 - Add `model`, no behaviour change

- Create `components/model/` with the structs and `Observable`.
- Populate models from the existing callbacks via shims. Screens still read their
  own fields. Models are shadow state, write-only.
- Add a host build for model unit tests. The project has no test harness; `model`
  has no ESP dependencies, so this is the one layer testable without flashing,
  and it is what makes the rest of the refactor verifiable.
- Verify: log model values, confirm they track what the screens display.

### Phase 2 - Unify input

- Add `InputRouter` and `InputEvent`.
- Move the touch/knob/media-SFX block from `DisplayController::update()` into it.
- Run dual-path (screens still poll) then cut over one screen at a time.
- Verify: every gesture still works, touch SFX still fire.

### Phase 3 - Split screens, lowest risk first

1. `InitializationScreen` - no touch, no MQTT, one input. Also delete the unused
   `setWiFiStatus()` / `setMQTTStatus()`.
2. `DeviceInfoScreen` - read-only plus one gesture.
3. `ModeController` to `ModeSelectView` + `ModeSelectController`. **Resolves the
   `Mode` enum collision** - rename both enums (`AppScreen` for the router,
   `KnobMode { LIGHT, CLIMATE, MEDIA }` for the selector). TEMPERATURE becomes
   CLIMATE, SOUND becomes MEDIA.
4. `EnvironmentInfoScreen` - 911 lines, the biggest. Arc animations stay in the view.
5. `SoundController` to `MediaView` + `MediaScreenController` + `MediaModel`, with
   `PcMediaAdapter` extracted from its MQTT code. Get this working against the PC
   target first; `SpotifyAdapter` is its own step afterwards (see Integrations).
6. `LightController` to `LightView` + `LightScreenController` + `LightModel`.
7. `KnobModel` becomes the single owner of the knob link - remove the duplicate
   `+`/`-` consumption from `CommandHandler`.

   **The screen is authoritative.** This is settled, not a choice: the knob's
   `setPoint` is a mode-agnostic detent counter with no physical meaning, so it
   cannot own a quantity it does not represent. The knob emits deltas; the screen
   owns volume and brightness.

   Consequences: stop treating the knob's coalesced absolute `setpoint=N` as a
   value - it is a burst count, so convert it to a delta or drop it. Drive
   `KnobModel::detentCount` from `+`/`-` only. `ScreenController::onEnter()` pushes
   the new mode's value down the currently-unused write path so the knob starts
   each mode in sync. The knob then needs no concept of modes at all.
8. CLIMATE screen - net new; the wheel's TEMPERATURE entry is dead today.
   `ClimateView` + `ClimateScreenController` + `ClimateModel`. Last, because
   nothing existing depends on it.

Per screen: extract view, extract controller, delete the old class, build, flash,
verify that screen.

### Phase 4 - Router replaces DisplayController

- `ScreenRouter` takes the mode state machine and the inactivity/auto-sleep logic.
- Navigation flags become `NavigationRequest` events.
- What remains of `DisplayController` is display power only - rename to
  `DisplayPowerController` or fold into the panel backend.
- Delete dead `transitionToMode()` and `CALIBRATE_ORIENTATION`.

### Phase 5 - Transport out of views

- `MqttModelBridge` owns all topics.
- Delete `setMQTTPublishCallback`, `setMQTTConnectedCallback`,
  `setMQTTConfiguredCallback`, `onMQTTMessage`, `onBrightnessReceived` from the
  screen classes.
- Delete the MQTT lambda in `main.cpp`.

### Phase 6 - Composition root

- `AppComposition` class. Target `main.cpp` around 80 lines: HAL init, build
  composition, start tasks.
- Replace raw `new` with `std::unique_ptr` owned by the composition. Precedent
  exists - `displayPanelBackend` is already a smart pointer while the other ~20
  globals are leaked `new`.

### Phase 7 - Fix CMake and clean up

- Rewrite every `REQUIRES` to match the layering.
- Drop `PRIV_REQUIRES controllers ui sleep_handler` from `display`.
- Confirm no cycles remain.

## Constraints

- **RAM.** ESP32-C6 has 512KB SRAM; a full 466x466x2 framebuffer is 434KB, so
  `LvglDisplay` runs double partial buffers in DMA-capable internal RAM
  (`MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`). The observer layer must not allocate
  per frame. All subscriptions wired at startup, fixed capacity, no heap in the
  render path.
- **Single core on C6.** Model mutation must be either queued and drained on
  displayTask, or guarded per aggregate. Pick one and apply it uniformly in
  Phase 1 - retrofitting it later means touching every model.
- **Scope.** 16,566 lines of first-party code excluding vendored. Phases 3 to 5
  are the bulk of the work.
- **Two trees exist.** This plan targets `screen/SW/ESP/esp-idf` only.
  `screen - Copy/SW/esp-idf` is a March 2026 snapshot predating the display
  backend refactor and is not a migration target.

## Active Files

Highest-churn files, by phase:

- Phase 3: `components/controllers/SoundController.{h,cpp}`,
  `components/controllers/LightController.{h,cpp}`,
  `components/ui/screens/EnvironmentInfoScreen.{h,cpp}`,
  `components/ui/screens/DeviceInfoScreen.{h,cpp}`,
  `components/ui/screens/ModeController.{h,cpp}`,
  `components/ui/screens/InitializationScreen.{h,cpp}`
- Phase 4: `components/display/DisplayController.{h,cpp}`
- Phase 5: `components/mqtt_manager/MQTTManager.h`
- Phase 6: `main/main.cpp`
- Phase 7: every `components/*/CMakeLists.txt`, `main/CMakeLists.txt`

## Screen Designs

Hi-fi mockups for ModeWheel, MediaSpotify, MediaPC, MediaTargetSelect, Climate
and MediaSeek (2026-09-12): https://claude.ai/code/artifact/586406e0-b823-4c72-83f3-07e82f455f59

Two directions were judged: Instrument (summed score 128) and Shell (127, named
winner by 2 of 3 judges on interaction and feasibility). The direction is not
chosen yet. Section 4 of the WBS is written against Instrument; re-point it if
Shell is chosen. Typographic scored 92 and is kept as a sketch only.

Correction to both design specs: the built-in LVGL Montserrat fonts (14/20/28/48
in managed_components/lvgl__lvgl/src/font/) are generated with
`-r 0x20-0x7F,0xB0,0x2022`, so the degree sign (U+00B0) renders. Temperature
strings may use it.

## Execution

The work breakdown structure that executes this plan is `dev_notes/mvc_refactor_wbs.md`
(2026-09-12): 318 tasks, 952h, 8 phases, 29 work packages, a derived critical path
of 86.5h, defect and dead-code registers, host-test strategy and the Phase 0
baseline measurements. Phase 0 blanks the WiFi credentials (BLD-20) before the
`p0-baseline` tag because the repository is public.
