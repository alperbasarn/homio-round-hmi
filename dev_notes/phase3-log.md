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

## MEDIA cluster, the pure half: MEDIA-02/03/04/05/07/08/09/10 and BLD-12 (2026-09-13, evening)

WP3.2 (the DEVICE_INFO split) is next on paper, but its sound row reads the SoundController
state, so splitting it now would regress the row until the MEDIA cluster exists. The bench-free
continuation that degrades nothing is that cluster's pure layer, in a new `media_target`
component that depends on `model` only.

| Piece | What landed |
|---|---|
| `media_target/IMediaTarget.h` | `MediaError` (Ok, NotAvailable, NotSupported, Busy, InvalidArgument, Transport), `MediaCaps` (volume, playPause, toggle, seek absolute/relative, trackInfo, position, `seekStepPercent`) and the virtual target: every call takes the caller clock and answers an error code; `tick()` once per frame. |
| `MediaTargetRegistry` + `NullMediaTarget` | Two fixed slots; `target(id)` never returns a null pointer: an unregistered slot answers a null target whose every call is NotAvailable. No heap. |
| `PcProtocol` | Encoders byte-identical to the three literals SoundController publishes (`setpoint:<n>`, `play`/`pause`, `request`), plus `toggle`, `request:v2`, `seek_abs:<pct>`, `seek_rel:<ms>`. `parseResponse` strips the `sender=` envelope (reporting the sender), takes the bare words, `setpoint:<n>` and `state:`/`volume:`/`position:`/`duration:`/`title:`/`player:` pairs in any order; a value out of range never sets its field; a comma inside a title survives; an empty payload sets nothing (it used to mute the device). |
| `PcMediaAdapter` | The PC app as a target behind an injected publish function. Volume: one publish per 120 ms window with the last value never lost, a settling retry 200 ms after the last detent, a failed publish kept pending and retried. Echo: a generation counter per sent setpoint; the PC reporting the value we last sent acknowledges it and never reaches the model, any other value while a publish is in flight (or the knob still turning) is dropped as stale until the 1.5 s window says the publish was lost. Play/pause gated on the last known state; `toggle` refused as Busy for 600 ms after any control until the reply lands. Seek default-closed until a v2 reply was seen. the v1 request word on link-up and on demand (a v2 PC answers the same word with the full tuple, so nothing is negotiated); an old PC answers without a position and the duration stays 0, which hides the position bar. Replies land as `MediaModel` records (IsPlaying, Volume, Position, SeekResolution, StateSynced, Availability, StateSeen); title and player go through the model pointer on the display task. |
| Build | `components/media_target` registered with `REQUIRES model` only; `controllers` gains the edge (BLD-43's deferred one); the host project compiles the component from the device files. |

A fact found on the way that the WBS rows only hint at: the PC app runs the same
`VK_MEDIA_PLAY_PAUSE` key for both `play` and `pause` (`PC/src/sound_controller_app.py`,
`handle_message`), so on the wire both verbs toggle. The old SoundController sent whichever the
screen thought was right; two quick taps before the reply could double-toggle. The adapter's
state gate and the 600 ms toggle suppression are the fix (MEDIA-08's row).

Verification: host suites `test_pc_protocol` (4 cases) and `test_media_target` (9 cases), 31
suites green; the 1.75 builds (0x1cc550, 42 % free); include edges clean (25 components).
Owed on the device (BLD-12's "a volume round trip works on device"): the composition does not
bind the adapter yet, so nothing changes on the board with this commit.

Not in this slice, deliberately: MEDIA-06 (the frame ring from the MQTT task) is the device
half and reuses `adapters/FrameRing`, the same way the LED bridge does; MEDIA-13/14/15/16 (the
screen controller, the knob binding machine, their tests, the SoundController deletion) come
with WP3.2/WP3.4. `MediaModel` still carries its own `volumePublish_`/`volumeEcho_` pair from
MDL-14; this design puts the coalescing and the echo in the adapter (MEDIA-07's row), so
MEDIA-13 must drive the target directly and MEDIA-16 retires the model pair.

Deviations from the rows: BLD-12 asks for `PRIV_REQUIRES mqtt_manager esp_timer`; neither is
needed because the publish function and the clock are injected, so the component has no ESP
include at all. MEDIA-07 says "generation-counter echo suppression"; the counter is per publish
and the acknowledgement is by value, since the v1 PC echoes no sequence. MEDIA-15's PC protocol
table exists (`test_pc_protocol.cpp`); its binding-machine half waits on MEDIA-14.

## MEDIA screen: MEDIA-13/14/15/16, MEDIA-06, CTRL-15 first cut (2026-09-13, night)

The SoundController is gone. The MEDIA screen is a `ScreenController` on the target registry,
hosted by `DisplayController` under the SOUND mode the way LIGHT is (tick under the LVGL lock,
navigation request polled once per frame), with its own view and a pure knob binding machine.

| Piece | What landed |
|---|---|
| `controllers/KnobBindingMachine` (MEDIA-14) | VOLUME / SEEK_IDLE / SEEK_ACCUM as a pure class taking a `MediaState` snapshot per call. VOLUME: every detent is one clamped SetVolume. SEEK: detents gather into one scrub position (1 % per detent, one 10 % step per detent on the coarse PC path, snapped to that grid), one SeekTo on the release that ends the scrub (consumed, so it is not a tap) or after 450 ms idle; an idle SEEK reverts to VOLUME after 6 s; back discards. Seek cannot be entered without a duration and a seek resolution. |
| `controllers/screens/MediaScreenController` (MEDIA-13) | `onEnter` builds the view, asks the active target for its state, resets the binding to VOLUME and sets the knob context; `tick` runs the phase (WaitForSync, Live, Unsynced after 3 s with no reply: the timeout the old controller never had, so an offline board renders, the knob works and a tap explains itself) and renders. Tap: `target.toggle`; a refusal is counted and flashed for 1.2 s, `Busy` (the adapter's toggle suppression) is silent. Knob: VolumeLocal record plus `target.setVolume`, or the scrub through the machine (PendingSeek records). Double tap: enter or leave SEEK. Swipe left/right: next / previous (D14). Hold: back to INFO, a scrub discarded. The rim is reserved for the target overlay (UIX-28). Never names a target. |
| `ui/views/MediaView` + `MediaScreenState` (CTRL-15, first cut) | R1 volume ring at the design's geometry (420 of 466 px, 25 px wide, rotation 120, 300 degrees), R2 timeline inside it (21 px, absent while the duration is unknown, white while scrubbing), the percent (48), the state line (Playing / Paused / Seek N% / Knob: seek), the target eyebrow, the scrolling title, a status line (Syncing..., Not available, the availability text) and the hint. Change-driven render. |
| `IMediaTarget::next/previous` | Added to the contract (the plan lists them); the PC adapter maps them onto `forward` / `rewind` (next-track / previous-track keys on the PC). |
| Device wiring (MEDIA-16, MEDIA-06) | `main.cpp` builds the registry, the PC adapter and the screen; `esp32/sound/*` messages are pushed into an 8-frame `FrameRing` on the MQTT task and drained into the adapter on the display task before the model drain; the link state is refreshed every frame; the adapters tick after the screen update. The adapter publishes through `publishRaw` (enqueue, QoS 0) with the same `sender=` envelope `MQTTManager::publish` builds, so the PC sees the same bytes and the display task never writes on the socket (the old controller did, and the screen entry now publishes under the LVGL lock). The DEVICE_INFO sound row reads the model volume and writes a VolumeLocal record plus `target.setVolume`; the PC placeholder page's play/pause calls `target.toggle`. |
| Retired | `SoundController.{h,cpp}`, its three MQTT callbacks and the `find("sound")` branch in main; `DisplayController` holds a `ScreenController*` for SOUND. `grep SoundController` over components, main and the tests is clean. |

Decisions taken on the way:

- The state request stays the v1 word `request`. The current PC app accepts any body containing
  `request` (`is_state_request`), so `request:v2` would have worked, but a v2 PC can simply
  answer the plain word with the full tuple and the adapter learns v2 from the reply
  (position or duration present). Nothing is negotiated; MEDIA-10's "v1 fallback" is therefore
  the absence of those fields, not a second request.
- MEDIA-13's "pushes volume to the knob" waits for the knob write path (WP3.6). `KnobModel`
  drops every burst as stale while a write is pending, and nothing sends the write yet, so a push
  now would wedge the replica; the controller sets the knob context only.
- The old controller published `setpoint:` at most every 25 ms and toggled on any tap; the new
  path coalesces to one publish per 120 ms and gates play/pause on the known state (the PC app
  maps both verbs onto one toggling media key).

Verification: host suites `test_knob_binding_machine` (4 cases) and `test_media_screen_controller`
(6 cases, with a fake target and a recording view), 33 suites green; the 1.75 builds (0x1ce300,
42 % free); include edges clean (25 components). Owed on the device, added to the runbook as A5
(offline) and step 9 (with the PC app): the MEDIA layout, the 3 s sync timeout, the knob and tap
paths, the echo-free ring, the coalesced setpoint rate, the no-double-toggle rule and the
DEVICE_INFO sound row.

Deviations: CTRL-15 asks for the three-ring stack measured against the design; this is the R1
and R2 geometry with the disc text, the R3 rail and the state views (UIX-18/19/25) remain.
UIX-28's gesture table is still unpublished; the double tap for SEEK and the reserved rim are
this cut's choices, written into `gesture_table.md`. CTRL-31 (navigation flags) is already met
by the outbox. The `MediaModel` volume limiter and echo suppressor from MDL-14 are unused now
that the adapter owns both; they go with the Phase 5 model clean-up.

## WP3.2 DeviceInfoScreen: SCRN-10, SCRN-11, SCRN-12 (2026-09-15)

The 859-line screen class is gone. DEVICE_INFO is now a view, a controller and one pure header
they share, hosted by `DisplayController` the way LIGHT and MEDIA are.

| Piece | What landed |
|---|---|
| `ui/views/DeviceInfoScreenState.h` | The view state (pointers to the connectivity and power state, the two percents, the Bluetooth flag) and the geometry both sides need: `layoutFor(panelW, panelH)` returns the bar rects, their widened touch rects, the bottom band and the two edge columns from the ported layout's own expressions, so a drag lands on the bar the user sees. Plus the two classifications the view used to hide: `signalLevels()` (0..4 over the dot and three arcs) and `batteryVisualFor()` (Unknown, Absent, NoReading, Critical, Ok, Charging). Pure and header-only; the host suite drives it directly. |
| `ui/views/DeviceInfoView` | The same layout as the ported page: the icon column (Bluetooth glyph, signal glyph, battery), the brightness and sound bars, the back chevron. Change-driven render against a snapshot. It owns LVGL handles only: the panel size is injected (the old `scalePx` read the display singleton, and `lvglReady` was latched before `buildUi` had run), it never drives the backlight and never pumps the LVGL timer. |
| `controllers/screens/DeviceInfoScreenController` | Reads the connectivity and power models every frame instead of polling four callbacks every 2 s; resolves the two drags with the shared geometry; the left bar moves the display brightness through an injected accessor pair, the right bar posts a `VolumeLocal` record and calls the active target's `setVolume`; a release on the bottom band requests INFO through the navigation outbox; a release on an edge leaves a page step the router applies; a swipe is consumed and a knob detent is not. |
| `DisplayController` | Hosts the controller: `tick` under the LVGL lock, the navigation request and the page step polled once per frame. New `enterDeviceInfo` / `exitDeviceInfo` / `applyBrightnessToPanel` put the first-paint dim where SCRN-23 wants it (the router dims, builds under the lock, pumps one frame, restores) instead of the 200 ms `vTaskDelay` fade the view used to run twice per navigation, which also desynced `displayIsOn`. |
| `main.cpp` | Builds the view and the controller, injects the brightness getter and setter, and drops the five callbacks of the old screen. The media registry now constructs before the page that uses it. |
| Retired | `ui/screens/DeviceInfoScreen.{h,cpp}`, `DeviceBatteryStatus`, `DeviceSoftwareUpdateState`, the network-details, Bluetooth and software-update callbacks, `lastActivityTime`, `resetScreen`, the never-attached `updateButtonEventHandler`, the two empty edge labels and the always-hidden "X" cross. `grep DeviceInfoScreen` over components, main and the tests is clean. |

Two defects the rows name are fixed. The signal glyph mapped the model's 0..4 bars onto three
arcs clamped at 3, so the top bar was unreachable and a weak-but-connected AP looked like a lost
one; the dot is now level 1 (always painted in the link colour, dimmed at zero bars so a far AP still shows the link) and the arcs are 2, 3 and 4. The battery icon painted one grey look
for "presence unknown", "no cell fitted" and "no percent yet", and ignored `charging` entirely;
the classification now has six values and the view paints five looks (Unknown and NoReading share
the grey outline, since the ported design has no glyph that tells them apart), and a reading is
believed whenever the cell is connected and the percent is valid, which is what the ADC boards
report with presence unknown.

What the old page tracked and never drew, now gone: the SSID and IP strings (read from the WiFi
manager every 2 s, compared, and never put on a label), the whole software-update state (six
fields, three of them `std::string`, polled every 2 s; release updates are not wired in this
tree), and `mqttConnected` (still carried in the view state for the row that will draw it, so
the model field has one consumer ready).

Verification: host suite `test_device_info_screen_controller` (14 cases: the shared geometry, all
five signal levels, every battery classification, both drags, the band and the two edges, the
consumed swipe, a model change with no touch, a drag leaving its bar, a null view and a null
accessor, an ADC-style reading with presence unknown, a resting finger doing no work, the hit-rect
overlaps and their precedence, an unpolled back request not outliving the screen, and
`NO_HEAP_SCOPE` over 400 frames), 34 suites green; the 1.75 builds (0x1cce70, 42 % free, 5 KB
smaller than the MEDIA commit); include edges clean (25 components). Owed on the
device: the page against its `p0-fixed` look, the four signal levels, a battery state, both
drags, the bottom band and the right edge, and that the entry no longer flashes.

Deviations from the rows: SCRN-10 describes "5 rows, 13 handles", which is the pre-port page; this
tree's ported page has the icon column and the two bars, and the port keeps that layout (the owner
decision that the ported screens are the base). SCRN-11's "one transition per swipe" is met by the
page consuming swipes, as it already did. The matrix page step is not a `NavigationRequest`,
which carries an `AppScreen`, but a small destructive intent beside it; DISP-11 retires the matrix
and with it this intent. Brightness has no model, so the composition injects a getter and a setter
pair; APP-28's persistence row will need a model to subscribe to.

### Review of the split, before it was committed (2026-09-15)

Four reader agents mapped the old screen, its wiring, the models and the battery side, a critic
checked them against the source, and five reviewers then went at the new code by dimension
(fidelity against the deleted screen, LVGL lifecycle and threading, model discipline, host test
quality, composition and build) with every finding handed to a second agent to refute. Eight
findings survived; all eight are fixed in this commit.

| What the review found | Fix |
|---|---|
| The battery classification tested presence before the reading, so a board that estimates the percent from the voltage and never learns presence (the ADC boards, and every board until the poller landed: `PERI-05`) would have greyed out a real reading. | The reading comes first, exactly as the old page gated it (connected and a valid percent), and the three unknowns only matter when there is no reading. Host case added. |
| A connected link at zero bars lit nothing: the dot was the first level and went to the trough colour, so a far AP looked like no glyph at all. The old page always painted the dot in the link colour. | The dot is the link mark again, always in the link colour, dimmed to half opacity at zero bars so zero and one bar stay apart. |
| The navigation outbox was never cleared on enter or exit. `update()` consumes a pending mode change (a `home` or `info` command over serial or MQTT) before it reaches this screen's case, so a tap on the bottom band in that frame left an intent nobody read, and the next entry to the page would bounce straight back to INFO with no touch. The old screen cleared its three request flags on every entry. | `cancelNavigationRequest()` in `onEnter` and `onExit`, with a host case. The same omission was in `LightScreenController` and `MediaScreenController`, reachable the same way; both fixed here too. |
| The view compared the whole `PowerState`, so the IIR-filtered voltage the battery poller posts on every sample repainted the glyph, and an LVGL style write invalidates its object whether or not the value changed. The old page carried 0.5 % and 20 mV deadbands for exactly this. | The snapshot holds only what the pixels depend on: the signal level and its colour, the battery look and its rounded percent, the two percents and the Bluetooth flag. |
| The battery fill read three LVGL getters whose answers are only valid because `lv_obj_align_to` forced a layout pass earlier in the build. | The fill rectangle is computed in `doBuild` from the numbers it already has. |
| A finger resting on a bar is one event per frame: the old page called the brightness setter (a panel write and a log line) and posted a volume record about sixty times a second. | Both setters return early when the percent has not changed, with a host case. |
| The envelope for a media publish was built with `std::string` on the display task, which a bar drag reaches about eight times a second. | A fixed buffer and one `snprintf`. |
| Two host cases were vacuous: the "inside the panel" assertion was `114 > 0`, and the "press outside both bars" probe was the panel centre, which is the middle of the sound bar, so it drove the volume while claiming nothing happened. | Real assertions, a dead probe to the right of the sound bar, and a new case pinning the hit-rect overlaps (the widened rects reach 6 px into the left edge column and 12 px into each other, resolved by test order, inherited verbatim from the old page). |

Refuted, after tracing: that the dim does not cover the first paint (the pump does flush before
the restore); that "no cell fitted" in the trough colour disappears (it reads as an unlit
outline, the same grey the bar troughs use); that the dead `esp_app_desc.h` include was harmful
(it was merely dead, and is gone); that `grep DeviceInfoScreen` must be empty in the notes too
(the WBS and the logs have to name what they retired).

Two residuals, both pre-existing and both owned by later rows: `handleMatrixNavigationGesture`
still admits DEVICE_INFO while `onInput` gates it out, now doubly dead (`DISP-12`); and
`WiFiManager::getSSID` / `getIPAddress` lost their only caller with the network-details callback,
so they are dead until a row draws those strings.
