# Phase 3 log: split screens

Working notes for Phase 3 of `mvc_refactor_wbs.md` (120 tasks). Each work package lists what
landed, how it was verified, and every deviation from the WBS row. Bench captures live in the
session scratchpad; the numbers quoted here come from them.

## WP3.0 Contracts and this phase's component edges (2026-09-13)

| Row | Status | What landed |
|---|---|---|
| BLD-43 component edges | done, one edge deferred | `ui` adds `theme` and `views` to INCLUDE_DIRS and `theme/Theme.cpp` to SRCS; `controllers` adds `screens` and `screens/NullScreenController.cpp`. `ui` and `controllers` already required `model` and `input` (BLD-42). `main/CMakeLists.txt` needed nothing. |
| SCRN-03 `IScreenView` | done | `ui/views/IScreenView.h`: non-virtual, idempotent `build(parent, w, h)` / `destroy()` / `setVisible()` over protected `doBuild` / `doDestroy` / `doSetVisible`; `root()`; `built()`, `visible()`, `panelWidth()`, `scalePx()`. Typed render is `ScreenView<State>::render(const State&)`, a no-op while unbuilt. The LVGL lock is a caller precondition: `LvglLock.h` defines `QNOB_LVGL_ASSERT_LOCKED()` on `LvglDisplay::lockedByCurrentTask()` (the recursive mutex holder is the calling task), live in debug device builds, compiled out under `NDEBUG` and on the host. |
| SCRN-04 `ScreenController` + `NavigationRequest` | done | `controllers/ScreenController.h`: `screen()`, `onEnter`, `onExit`, `onInput` (returns consumed), `tick`; protected `requestScreen()` / `cancelNavigationRequest()`; the router reads `takeNavigationRequest()` once per frame. `controllers/NavigationRequest.h`: `PendingNavigation` (latest intent wins, destructive `take()`), payload `model::NavigationRequest`. `controllers/screens/NullScreenController.{h,cpp}`: the stand-in for screens not split yet. |

Verification: host suite `test_screen_contracts` (6 cases): build x2 then destroy x2 is one
build, one destroy and no heap; render and visibility reach the view only while built; scalePx
from the injected width, never 0; a request is taken exactly once; the latest request replaces an
unconsumed one and can be cancelled; the null controller consumes and requests nothing. Device:the 1.75 builds and boots (`ui/theme` and `controllers/screens` compile and link).

Deviations from the rows:

- `IScreenView.h` lives in `ui/views/` (the row says `ui/IScreenView.h`); with both directories
  on the include path the include line is the same, and the view contract sits with the views.
- The lock assert needed a header without FreeRTOS in it so the contract compiles on the host:
  `ui/LvglLock.h` (not listed in the row) carries the macro; `LvglDisplay.{h,cpp}` gained only
  `lockedByCurrentTask()`.
- `controllers` does not yet require `media_target`: that component does not exist until
  BLD-12 (MEDIA-02), which depends on this row; BLD-12 adds the edge when it registers it.
- `controllers/screens/` needed a real file to compile and link, so `NullScreenController` is
  the first occupant: the Phase 4 router will hold it for every screen still owned by the old
  classes, and the host suite uses it as the stub.
- `onInput` returns a bool (consumed) instead of the void the old screens use, so the router can
  stop propagating an event the screen handled; the old screens keep their signature until each
  is split.
- The `build()` signature injects the panel size (`SCRN-10`: `scalePx` must not dereference a
  null display); the row lists only `build()`.

## WP3.1 InitializationScreen (2026-09-13)

| Row | Status | What landed |
|---|---|---|
| SCRN-07 `InitializationView` | done | `ui/views/InitializationView.{h,cpp}`: `ScreenView<model::NavigationState>`; arc (start 120, span 300, range 0..100), black disc, red Q and white NOB, Montserrat 32 on panels of 400 px and up; `doRender` eases the arc to `initProgressPct` over 500 ms (presentation state only); `doDestroy` deletes the animation and the tree; `doSetVisible` toggles the hidden flag. Geometry from the injected panel size. |
| SCRN-08 `BootScreenController` (folds DISP-09) | done, device check owed | `controllers/screens/BootScreenController.{h,cpp}`: one start stamp at `onEnter` (a second enter while active keeps it), one ramp 0..100 over 2500 ms posted as `InitProgress` records, the view rendered from `NavigationModel::state()`, one `Info` request 400 ms after the ramp completes. `DisplayController` hosts it: `init()` enters it once under the LVGL lock, `update()` ticks it under the lock and pumps LVGL, the handoff waits for `markCompositionComplete()`, `setMode` exits and re-enters it; the old ramp members, the two constants and the INITIALIZATION branch of the auto-switch are gone. The dead `setWiFiStatus` / `setMQTTStatus` went with the screen. |
| SCRN-09 retire `InitializationScreen` | done, device check owed | `ui/screens/InitializationScreen.{h,cpp}` deleted (`grep InitializationScreen` is empty). `startupSplashTask`, its handle, flag and stack constant, and the 30 ms pseudo-join are gone from `main.cpp`. The display task now starts right after LVGL init, before touch, audio, WiFi and MQTT; until app_main publishes `s_compositionComplete` (acquire/release) the frame loop runs only the drain and the boot controller, so the arc animates through the whole setup. |

Verification: host suite `test_boot_screen_controller` (4 cases): the ramp reaches 100 on the first
frame at or after 2500 ms and the request lands exactly 400 ms later, the model accepts every step
(0 rejected records) and never moves backwards, progress is exactly 50 at 1250 ms and 100 at
2500 ms with no request at 2899 ms, nothing before `onEnter`, one stamp per run, `onExit` destroys
the view, and the ramp allocates nothing. 24 suites green; the 1.75 builds; include edges clean.
Owed on the device (the board is off the bus): progress monotonic once, INFO about 400 ms after
100 %, the arc visibly moving during SD and WiFi init, no second ramp.

Deviations from the rows:

- No percent label and no status line: the row inherited them from the pre-port splash. The
  screen ported from main (the look the owner accepted on 2026-09-13) has the arc and the logo
  only, so the view renders exactly that and the `ConnectivityModel` binding is unused here. The
  SCRN-08 device check about the status line therefore does not apply.
- The handoff moved from a 2900 ms wall clock in `DisplayController::update()` to the
  controller request, gated on composition completeness; with the display task starting early,
  INFO arrives at about 2.9 s after LVGL init when setup is faster than that, otherwise as soon
  as app_main finishes. Before this change the ramp restarted when the display task started
  (the second ramp DISP-09 describes) and INFO came at about 5.6 s.
- `appModel.wireSubscriptions()` (the MDL-30 seal) moved from the end of app_main to just
  before the display task starts, so the first drain can never race a subscription.
- The LVGL pump for the boot screen (`LvglDisplay::taskHandler()`) is called by
  `DisplayController::update()` for the INITIALIZATION mode; the other modes still pump from
  inside their screens until each is split.

## LED integration, the host half: LED-01, LED-02, LED-03 and LED-07 without a client (2026-09-13)

Owner pick while the bench is unavailable: the model layer and everything pure around it.

| Row | Status | What landed |
|---|---|---|
| LED-01 `LedRegistryModel` | done | `model/LedRegistryModel.{h,cpp}`: eight fixed slots {id[16], name[24], used, paired, online, stateKnown, lastSeenMs, state{power, brightness 0..100, rgb, effect, params[5], effectRgb, count}, lastSource, lastSeq, stateGeneration}, the selected slot, `refusedAnnounces`. Display-task setters (`announce`, `setOnline`, `noteSeen`, `applyState`, per-field setters, `setPaired`, `select`, `forget`) and records whose field byte is `(kind << 3) | slot`, so two strips never coalesce into each other. `tick()` applies the 120 s presence timeout. `LedEffect` (solid, glow, rainbow, chase, sparkle, wave, fire, aurora, in that order so the old LedMode numbering survives) and `LedSource` live here. |
| LED-02 `LightModel` reshape | done | `LightState` is now {power, brightness, hue, rgb, effect, params[5], effectRgb, stripSlot}; `LedMode`, `Field::Mode`, `cycleMode`, saturation are gone. Local setters mark fields pending (`Pending*` bits) and the 100 ms limiter; `takePending()` hands the bridge the fields and a fresh seq; `adoptRemote()` applies a `state` from the strip without ever marking a publish. `AppModel::followSelectedStrip()` mirrors the selected registry slot into the light state on every drain (a selection change adopts the reported tuple; a new generation adopts through the echo rule). `ColorMath` gained `rgbToHue` / `rgbFromHue`. |
| LED-03, codec and routing | host half done | `adapters/LedTopics.{h,cpp}` (the `homio/led/<id>/<leaf>` family, subscriptions, the `qnob-` controller id), `adapters/LedJson.{h,cpp}` (a flat in-place JSON reader, no cJSON, no heap; decoders for `info`, `status`, `state`, `controllers`; encoders for `set`, `pair`, the pair confirm and `unpair`), `adapters/LedBridgeCore.{h,cpp}` (routes inbound into the registry, emits one `set` per window for the selected strip, refuses commands to a strip that is offline or not mine, runs the pairing state machine with the 60 s window). All three compile on the host and on the device (`ADAPTERS_PURE_SOURCES`). Not done: the device wrapper (`MqttModelBridge` subscribing through `MQTTManager`, the inbound frame ring across tasks, the retirement of `ledRing/*` and `esp32/light/*`). |
| LED-07 fake-broker tests | host half done | `test_led_bridge.cpp`: a fake broker with retained topics and a fake strip answering as the contract says. Nine cases: available-not-mine with a refused local command, one-tap pairing of a fresh strip and unpair, the blink code with a wrong code timing out, a `set` carrying only the changed field with a silent echo, a 2 s drag at 19..22 sets with the last value landing, a web change applied without an echo publish, LWT offline refusing commands and online restoring them, a retained replay changing nothing, the ninth strip refused, and no allocation on the path. |

Verification: 27 host suites green (`test_led_registry_model` 6 cases, `test_light_model` 7,
`test_led_json` 6, `test_led_bridge` 9; every older suite adapted where the light state changed);
the 1.75 builds; include edges clean; `LightController` posts `Field::Effect` where it posted
`Field::Mode`. Measured: `AppModel` 6968 B, `LedRegistryModel` 2672 B (three copies of the state:
working, last notified, snapshot) against the 1.2 KB the contract guessed; the 8 KB cap holds.

Deviations and open points:

- Strings (id, name) never travel in a `ModelUpdate`; they reach the registry through the
  display-task setters, the same way the media path carries titles. The bridge drains its inbound
  frames on the display task and calls them; the records exist for numeric, cross-task producers.
- Echo rule refined against the contract text: an echo with a seq below the last sent one is a
  stale echo and is dropped (applying it would flash the older value); the echo of the last set is
  applied except for fields with a newer local change still pending; any remote source wins in full
  and cancels what was pending.
- A command to a strip that is offline or not mine is discarded at once (`discardPending`), which
  is the contract "refused locally, no queueing".
- The 120 s presence rule needs the strip to say something within 120 s. It publishes `state` on
  changes only, so an idle strip goes grey after two minutes although it is online. Proposed for
  the LED side: refresh the retained `status` every 60 s (one tiny publish), or the HMI drops the
  timeout and trusts the LWT. Recorded in the contract as an open point.
- `LedBridgeCore` is a new pure class between the codec and the device bridge (the row names only
  `MqttModelBridge`); it exists so the whole LED path is testable without a client.

## LED integration, the device half of LED-03 (2026-09-13)

| Piece | What landed |
|---|---|
| `MQTTManager` | `publishRaw()` (no `sender=` prefix: the LED documents are plain JSON and the strip parses them as such) and `addSubscription()` (a list re-subscribed on every connect, and at once when already connected). The hard-coded sound and command subscriptions are untouched. |
| `adapters/FrameRing.h` | A single-producer single-consumer ring of text frames (topic 64, payload 320, twelve slots), acquire/release on two indices, drop and oversize counters, high-water mark. Host suite `test_frame_ring` (3 cases). |
| `adapters/MqttModelBridge.{h,cpp}` | The device half: the controller id from the STA MAC (`qnob-` + three bytes), the four inbound subscriptions, `onMqttMessage()` on the MQTT event task copying `homio/led/...` frames into the ring, `pumpInbound()` before the drain and `pumpOutbound()` after the screen update on the display task (console requests, pairing timeout, the publishes through `publishRaw` at QoS 1), one warning each for a refused ninth strip and a malformed document, and two snapshot-based descriptions for the console. |
| Console (`CommandHandler`) | `leds`, `ledStatus`, `ledSelect:<slot>`, `ledPair:<slot>`, `ledCode:<1-6>`, `ledUnpair:<slot>` (serial only, not in the MQTT allow-list). The requests cross to the display task through atomics; the listing reads the registry snapshot. This is the bench UI until the LIGHT screen on the registry (LED-05). |
| `main.cpp` | The bridge is built right after the MQTT manager, the message callback hands it every LED topic first, the display frame pumps it, the command handler gets it. |

Verification: 28 host suites green; the 1.75 builds (0x1c2ad0, the codec and bridge now linked);
include edges clean. Owed on the bench: a broker configured on the knob
(`configureMQTTServer:host:port[:user:pass]`), a strip on the `dev` branch of the LED firmware
pointed at the same broker, then `leds` showing it, `ledPair`, `ledSelect`, a brightness change
from the console reaching the strip, and the two-strip gate (LED-08).

Deviations from the row: `ledRing/*` and `esp32/light/*` are not retired yet; the old LIGHT screen
still publishes to them and stays usable until LED-05 replaces it. The knob does not persist
pairings yet (LED-06), so after a reboot a strip is "available" until its `controllers` document
replays from the broker (it is retained, so this takes one subscribe). The bridge holds the old
`sender=`-prefixed path for every other topic; Phase 5 moves them over.

## LED-05 LIGHT screen on the registry, the screen half of LED-04, CTRL-21/22/23 (2026-09-13)

| Piece | What landed |
|---|---|
| Pairing in the model | `LedRegistryState` carries the flow state (`pairing`, `pairingSlot`, written by the bridge core) and three single-consumption intents (`pairIntent`, `codeIntent`, `unpairIntent`) with records `PairIntent`, `CodeIntent`, `UnpairIntent`. The bridge core takes each intent once in its tick and writes the state back (Paired is written the moment `controllers` names this HMI). The console commands post these records; the bridge lost its request atomics. |
| `ui/views/LightScreenState.h` | The view state (light + registry + the dial) and the rim geometry both sides share: angle from 12 o'clock, equal segments in slot order, chooser visible when there is a choice or the only strip is unpaired, hue = angle / 360. |
| `controllers/screens/LightScreenController` | Disc tap toggles power; knob detents post `BrightnessDelta`; swipe left/right posts `EffectStep`; a rim press that moves past 6 px is a ring drag posting `Hue` from the angle, and its lift (a swipe to the driver) is consumed; a rim tap selects the paired strip under the segment; a 3 s hold on an unpaired segment posts `PairIntent`; a 3 s hold anywhere else after no drag requests INFO (CTRL-22: a drag-and-hold never backs out); while a pairing is requested the knob dials 1..6 and a tap posts `CodeIntent`. Every control is a record; nothing touches LVGL. |
| `ui/views/LightView` | The p0-fixed fill in the swatch colour with the percent (30 % opacity when off), the effect name, the strip name with an online badge, eight rim arcs (mine in the strip colour, offline grey-blue, unpaired grey, the selected one full opacity), the pairing prompt with the dialled count. Renders only the parts whose inputs changed (CTRL-21). |
| Retired | `controllers/LightController.{h,cpp}` with `enum LedMode`, its three MQTT callbacks, the `ledRing/*` publishes and the `esp32/light/brightness` inbound branch in main. `DisplayController` hosts the new controller like the boot one (enter/exit/tick under the lock, `onInput` consumed stops the matrix defaults, the console setpoint path becomes a knob event). CTRL-23 is settled: `grep LedMode` is empty. |

Verification: host suite `test_light_screen_controller` (6 cases) plus the intent cases in the
registry and bridge suites; 29 suites green; the 1.75 builds (0x1c2f20); include edges clean.
Owed on the device: the LIGHT layout against the p0-fixed screenshot with the chooser as the only
addition, two paired strips switching on the rim, an offline badge within 45 s, the blink prompt.

Deviations: hue comes from the angle of the ring drag, not from the x position the old screen
used (the row says "ring drag"); the chooser also shows for a single unpaired strip so the pair
affordance is reachable; the LVGL lock is still taken by the host for every LIGHT frame (the row
asks for none on idle frames), the view merely does no LVGL work then.

## Independent review of the day's device-facing code (2026-09-13, evening)

Two review agents, one on boot order, threading and LVGL locking, one on the LED path and the
contract across both firmwares, each told to refute rather than confirm. Every finding below was
re-read in the code before it was acted on. Neither found a crash in the code written today; the
threading of the early display task, the lock discipline, the LVGL 8.4 API use, the RTC black box
and the sleep gates were checked and held. What did not hold, and what changed:

| Finding | Fix |
|---|---|
| `home` / `info` console and MQTT commands called `DisplayController::setMode()` on the command or MQTT task while the display task renders (pre-existing, real LVGL corruption risk). | `DisplayController::requestMode()` stores the pending mode; `update()` applies it on the display task. |
| A `set` was taken from the light model and lost when the publish failed (broker down while the strip still counted as online); the recovery replay was then dropped as a stale echo. | `pumpOutbound()` publishes only while connected; a failed publish hands the fields back (`restorePending`); the echo rule drops an older seq only while a newer set is pending or in flight (`ackedSeq`), otherwise a replay is applied as the truth. |
| Any `state` carrying `rejected:true` unpaired this knob, whoever was rejected, and the strip retained rejections as the state every later subscriber replayed. | The knob unpairs only when the rejection names its own id; the strip publishes rejections without the retain flag. |
| The rim chooser was drawn at 200..219 px while the router's rim annulus is 140..190 px: a tap or a hold on a visible segment was a dead-zone event, and a hold on an unpaired segment backed out to INFO. | The arcs are sized to the annulus (diameter 380, width 50 in the 466 px space). |
| The 12-frame inbound ring is smaller than the retained burst of four documents per strip on every reconnect. | 33 frames (four per slot plus the free one). |
| `esp_mqtt_client_publish` ran inline on the display task and can block for the network timeout when the socket is full. | `publishRaw` enqueues into the outbox (`esp_mqtt_client_enqueue`, stored) with an 8 KB outbox limit. |
| `leds` copies the 2.7 KB registry snapshot onto the 4 KB command task stack. | Command task stack 8 KB. |
| Nothing ever selected a strip after boot, so every knob turn was discarded, and the single-strip case hid the chooser while asking for it. | The first strip that becomes mine is selected when nothing is; the chooser shows while nothing is selected. |
| A brightness-only `set` switched a strip the user had just turned off back on (the web page rule applied to MQTT). | The strip couples power to brightness only for the full-tuple web path. |
| The LED timers were fed the 32-bit frame clock (a wrap after 49.7 days stops the publishes). | The pumps take the 64-bit clock. |
| A pair intent for a second strip while a flow was running was consumed and refused silently. | The latest request wins: the running flow is dropped and the new one starts. |
| The RTC black box was one record rewritten in place (a reset mid-write loses it); the LIGHT hint was 600 px wide on a 466 px panel and the small labels used the 14 px face. | Two alternating records, the newer valid one wins; wrapped, centred hint and prompt; Montserrat 28 for the name and effect. |

Left as noted, not changed: re-entering INITIALIZATION would show a full arc (no caller exists);
the strip prints `seq` as a 32-bit long (unreachable in practice); the LVGL lock is still taken
for every LIGHT frame.

Verification after the fixes: 29 host suites green with five new cases; the 1.75 builds (the
Montserrat 28 face now links, +38 KB); include edges clean; the strip's 141 checks and its pinned
build are green (+16 B).

## Threading audit of the legacy screens (2026-09-13, evening)

Every path from a task other than the display task into a screen, the display controller or
LVGL, read and classified:

| Path | Task | Verdict |
|---|---|---|
| `home` / `info` commands | command, MQTT | fixed in the review round: `requestMode()` |
| `setpoint` command | command, MQTT | clean: a Burst record through the queue plus the atomic knob delta |
| `calibrateOrientation` and the named screens | command | clean: `showNamedScreen()` stores atomics |
| LED console commands | command | clean: registry records through the queue |
| `soundController->onMQTTMessage()` | MQTT | **fixed**: the message was a `std::string` assigned on the MQTT task and read on the display task (a heap reallocation race). It now crosses through a 160-byte buffer behind a spinlock; the string is touched on the display task only. |
| `infoScreen->updateIndoorTemperature()` | MQTT | **fixed**: a float and a flag written from the MQTT task. The screen now reads the indoor value from the environment model through a callback, the way it already reads the outdoor one; the two direct setters are gone. The model record was already posted, so nothing else changes. |
| `wifiManager->getSSID()` / `getIPAddress()` from the DEVICE_INFO callbacks | display (reads) vs network (writes) | clean: the getters copy under `text_lock` |
| the three pollers | network | clean: records only |
| `sleepHandler` callbacks (display off, pre-sleep) | display | clean |
| `placeholderPageScreen` play/pause, DEVICE_INFO brightness and sound rows | display | clean |
| `KnobController::getReceivedCommand()` | command | clean: spinlock (WP2.4) |
| `publishResponse()` inside the MQTT event handler | MQTT | acceptable: QoS 0 publish from the event task; QoS 1 there could deadlock the client (the strip avoids it), so it stays QoS 0 |

Verification: the 1.75 builds; nothing host-testable changed. Device check: the sound `state:`
messages still move the SOUND screen, and `IN` on the INFO screen still follows the indoor topic.
