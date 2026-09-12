# Phase 1 log — Floor, then `model`

Executed against `dev_notes/mvc_refactor_wbs.md` Phase 1. Started 2026-09-12 on
the primary target (ESP32-S3-Touch-AMOLED-1.75); all three variants must build.

## WP1.0 Floor: crashes, secrets, and the four never-worked paths

| Task | Status | What changed | Verification |
|---|---|---|---|
| APP-01 numeric validator | done | `CommandHandler::parseInt()` (strtol, whole-string, range-checked) replaces the bare `std::stoi` in `cmdKnobSetpoint` that aborted the chip on `setpoint:abc`; `connectWifi`'s slot parse uses it too. Rejections log and reply `setpoint:ERR:invalid_value`. | builds; bench: `setpoint:abc` over serial and MQTT logs a rejection, no reboot |
| APP-03 command trust tags | done | `CommandSource {Serial, Knob, Mqtt}` on every `processCommand`. Commands carry `allowedFromMqtt`; the MQTT allowlist is `+ - setpoint home info getDeviceName showNetworks commInfo showStaticIP listNVSValues startSoundRecord stopSoundRecord playLastSoundRecord otaInfo otaStatus help`. Refused from MQTT: `reset clearNVS connectWifi configureMQTTServer configureStaticIP enableStaticIP disableStaticIP setDeviceName otaUpdate calibrateOrientation`, with `<cmd>:ERR:refused_from_mqtt` published. `calibrateOrientation` is now a logged no-op from every source: `CALIBRATE_ORIENTATION` has no case in `DisplayController::update()`, so entering it froze the UI (no screen update, no LVGL pump) until the 30 s inactivity timeout. | builds; bench: `connectWifi` over MQTT refused with NVS unchanged; same command over serial executes; UI stays responsive |
| APP-04 console accumulator bound | done | `kMaxConsoleLineBytes = 512`; a newline-less stream over that is dropped with one warning. | builds; bench: 2 KB with no newline gives one warning and flat heap |
| APP-05 command task priority | done | `COMMAND_TASK_PRIORITY 1` and `COMMAND_STACK_SIZE 4096` in `hal_config.h`; the task ran at priority 5, above the renderer, and preempted every frame on the single-core C6. | builds; bench: commands still land under 100 ms, no frame overrun during a burst |
| APP-24 hidden reload | done | `listNVSValues` no longer calls `loadAllConfigurations()`, which silently discarded uncommitted RAM values. | builds; bench: no reload line, RAM values survive |
| PCKNOB-02 HiveMQ purge | done (rotation pending) | `esp-idf/PC/src/mqtt_handler.py` and `config_handler.py` carry no broker defaults; `connect()` reports an unconfigured broker instead of raising; the config path resolves next to the script, not the CWD. `qnob_config.json` untracked and ignored; `qnob_config.example.json` tracked. `git grep -i hivemq` on the index: only the docs. The values are still in both repos' history and in `QNOB/PC` and `QNOB/screen/SW/PC` on disk (kept by owner choice); rotation at HiveMQ is PCKNOB-02b. | `git grep` clean; PC app: missing config gives a clear message |
| PCKNOB-03 C6 knob pin conflict | done (gate path) | see `phase0-log.md`: `CONFIG_QNOB_ENABLE_KNOB_UART` defaults off on the C6. Q1 stays open. | C6 builds with the knob driver out |
| CTRL-01 instrument silent failures | done | `SoundController::processIncomingMessage` logs `Inbound sound message matched no branch`; `LightController::sendSetpointToServer` logs `light gate closed: <reason>` (throttled to one per 2 s); `CommandHandler` and `DisplayController` each log the knob token they consume, so a detent shows two consumers. | builds; bench: the three log lines appear |
| CTRL-02 `sender=` prefix | done, at the dispatch layer | `MQTTManager::handleMQTTEvent` strips `sender=<id>,` once and never dispatches the device's own echoes; consumers receive the body. This was the dead PC round-trip: `on_message` handed the raw `sender=PC,state:playing,volume:N` to the sound parser and `sender=PC,<command>` to the command table, so nothing matched. Done here rather than in `SoundController` because the command topic was dead for the same reason. | builds; bench: `State received` after a PC reply; a disc tap toggles PC playback |
| CTRL-03 light publish gate | done | The configured callback in `main.cpp` now accepts the single configured client (`hasLightMQTTConfigured() \|\| hasSoundMQTTConfigured()`); `lightMQTTServerURL` has no writer, so LIGHT had never published. One-client-or-two is still NET-20. | builds; bench: `ledRing/#` receives colour, brightness and mode on a drag |
| CTRL-04 light rate limit | done | Redraw sets `publishPending`; publishes run at most every `kPublishIntervalMs = 25` (40/s, three topics) with a trailing flush. Unthrottled it was up to 300 msg/s. | builds; bench: count at the broker, last message equals the screen |
| NET-02 dispatch log | done | `dispatch topic=<t> prefixed=<0/1> self=<0/1> sender=<id> len=<n>` at every MQTT_EVENT_DATA. | bench: the line shows `prefixed=1` on PC traffic |
| UIX-01 character set | done | `dev_notes/character-set.md` (D9). | n/a |
| UIX-02 theme config | done | `sdkconfig.defaults`: `LV_THEME_DEFAULT_DARK=y`, `GROW=n`, `TRANSITION_TIME=0`, `LV_USE_QRCODE=n`; `REFR_PERIOD` untouched at 30. Applies on a regenerated `sdkconfig` (the per-machine generated files were deleted). | builds; bench: no light rectangle, no press scaling; binary delta below |
| BLD-40 re-capture and tag `p0-fixed` | **pending — bench** | Needs the 1.75 on the bench: four screenshots under the dark theme, the gesture checklist re-ticked with PC play/pause and LIGHT publish passing. The tag waits for that. | |

### Build evidence (2026-09-12, all three variants clean)

| Variant | `qnob-screen.bin` at `p0-baseline` | after KNOB-175 / PCKNOB-03 | after WP1.0 | Delta from baseline |
|---|---|---|---|---|
| esp32s3_amoled175 (primary) | 1,568,656 | 1,584,448 | 1,587,312 | +18,656 (knob driver in, QR out, new code) |
| esp32c6_amoled143 | 1,748,000 | 1,730,320 | 1,732,544 | −15,456 (knob driver out) |
| esp32s3_lcd128 | 1,542,656 | n/a | 1,545,568 | +2,912 |

Regenerated configs on all three: `LV_THEME_DEFAULT_DARK=y`, `GROW` off,
`TRANSITION_TIME=0`, `LV_USE_QRCODE` off; knob UART on for both S3 boards, off
for the C6. No first-party compiler warnings; LVGL's own `lv_theme_default.c`
warns about an unused `trans_props` once transitions are 0 (vendor code). The
component-graph dump still shows the two cycles on every variant, as expected
before Phase 7.

## Behaviour changes carried into the `p0-fixed` reference

These are the intentional deltas against `p0-baseline` that BLD-40 records:

1. Dark LVGL theme; no press-grow; no style transitions; QR widget compiled out.
2. `calibrateOrientation` no longer enters the unhandled mode (no 30 s freeze).
3. Secret, destructive and UI-freezing commands are refused from MQTT.
4. `setpoint:<non-number>` rejects instead of rebooting.
5. The PC play/pause and state round-trip works; the device ignores its own echoes.
6. LIGHT publishes to `ledRing/#`, at most 40 msg/s.
7. The command task runs below the renderer.
8. Knob UART on the 1.75 (GPIO16/17); off on the C6.

## WP1.1 Component, harness, the three irreversible decisions

Started 2026-09-12 with the owner's go-ahead, before BLD-40 (additive shadow layer, no
behaviour change; P1b diffs against `p0-fixed` once it exists).

| Task | Status | What changed | Verification |
|---|---|---|---|
| MDL-01 RAM budget | done, provisional | 4 KB all-in for the model layer: 1,024 B queue (64 × 16 B `ModelUpdate`; the WBS estimated 20 B, the POD is 16), ≈0.5 KB subscriber arrays (8 models × 8 × 8 B), ≈1.2 KB structs (three `FixedString<64>` in MediaModel dominate), ≈1 KB snapshot copies. Measure with `idf.py -B "<build>" size-components \| grep libmodel` (`.bss` + `.data`) and the `sizeof` asserts the host suites print. | on-device `libmodel.a` row recorded below |
| BLD-05 dual-mode CMake | done | `components/model/CMakeLists.txt` branches on `ESP_PLATFORM`; both modes read one `sources.cmake`; no project-local REQUIRES. | host configure with `IDF_PATH` unset; IDF build below |
| BLD-24 `adapters` component | done | Registered with `REQUIRES model`, private edges to the drivers (D15); one placeholder source so it links as a normal static library. | IDF build below |
| BLD-41 phase edges | done | `model` added to REQUIRES of `controllers`, `display`, `ui`, `command_handler`, `storage`, `main`; `adapters` on `main`. Additive; Phase 7 tightens. | IDF build below |
| BLD-06 host harness | done | `esp-idf/test/host/`: single-header runner (`TEST_CASE`/`CHECK`/`REQUIRE`), `FakeClock`, `FakeTransport`, `FakeMediaTarget`; `Hardware Ref/build_host_tests` is the ignored build dir. | `ctest` 3/3 with `IDF_PATH` unset; the fakes include no ESP header |
| BLD-07 device flags + sanitizers | done | `-std=gnu++2b -fexceptions -fno-rtti -Wall -Wextra -Werror -fsanitize=address,undefined -fno-sanitize=vptr`. | a deliberate stack out-of-bounds read aborted under ASan (`stack-buffer-overflow`) |
| BLD-08 purity guard | done | `check_model_purity.sh` rejects platform `#include`s, `CONFIG_` symbols and platform calls under `model/`. | exit 0 clean; exit 1 with an injected `esp_log.h` include |
| BLD-09 runners | done | `run-host-tests.sh` (never sources the IDF export; compiler is `g++`) and `run-host-tests.ps1` (delegates to WSL). | both print the ctest summary; compiler reported as `/usr/bin/g++` |
| BLD-10 glob registration | done | `file(GLOB ... CONFIGURE_DEPENDS test_*.cpp)`, one executable and ctest entry per file. | a temporary `test_zzz_*.cpp` appeared in `ctest -N` and vanished after deletion, no CMake edit |
| MDL-02 value types + clock | done | `ModelTypes.h`: `Millis`, `Percent` + `clampPercent`, `Celsius` with `kNoTemperature = -1000` (not the -1.0f the old code used, which is a real temperature), `FixedString<N>` (truncating, terminated, never allocates). `Clock.h`: injectable function pointer + context. | purity guard green; used by every suite |
| MDL-03 mutation queue (D1 + D18) | done | `MutationQueue`: 64 fixed slots, coalesce-in-place keyed on (aggregate, field), never-drop retries 3 × 2 ms then `MUTQ overflow never-drop cls=1` + fault counter, overflow counter, `drain()` with a consumer-task guard that counts off-task drains. All platform contact is through injectable lock/sleep/log/guard hooks, so the device installs a critical section and the host installs nothing. | `test_mutation_queue`: 10 cases green, incl. 100 volume records collapsing to one slot in FIFO position, capacity+1 for both classes, lock hooks bracketing every operation, zero allocations |
| MDL-04 `Observable<T,Max>` (D3) | done | Fixed function-pointer + context array, `subscribe()` refuses when full, `set()` notifies on change only. | `test_observable`: 6 cases green, 10k sets with zero allocations |
| MDL-06 `HeapGuard` / `NoHeapScope` | done | Global `operator new`/`delete` replaced with a counter; `NO_HEAP_SCOPE()` fails the enclosing case on any allocation. | the probe above; every suite uses it |
| NET-05 `ModelSnapshot<T>` (D2) | done | Seqlock over a trivially-copyable payload; `publish()` odd/even sequence with release fences, `read()` retries on odd or changed sequence, `tryRead()` for one attempt. | `test_model_snapshot`: two readers vs one writer, 100k publishes, zero torn reads; generation 200,000; zero allocations |
| MDL-40, MDL-41 suites | done | see above | green |
| MDL-30 `AppModel` | skeleton | Holds the queue and clock, `wireSubscriptions()` returns false on a second call, `drainPendingMutations()` drains into `apply()`, which counts every record as unrouted until WP1.3 adds the models. | completes with WP1.3 |

Host run: `bash run-host-tests.sh` (or `.\run-host-tests.ps1`) from `screen/SW/ESP`.

On-device (1.75 build after WP1.1, commit `2e21074`): `model` registers with `reqs=[] priv=[]`,
`adapters` with `reqs=[model]` and the four drivers private; every BLD-41 edge appears in the
component graph (24 first-party components, 67 edges, still the two cycles). The binary is
unchanged at 1,587,312 B because nothing links `libmodel.a` yet; its `size-components` row, and
with it the MDL-01 measurement, become real when APP-06 constructs `AppModel`.

## WP1.2 Domain rules lifted out of views

Started 2026-09-12 after WP1.1. Every rule below now has one home under `components/model/`
(or, for the command grammar, an ESP-free leaf inside `command_handler`) and a host suite that
pins it; the views and drivers call the model and own nothing numeric. Behaviour is unchanged
except where a row says otherwise.

| Task | Status | What changed | Verification |
|---|---|---|---|
| MDL-07 `Bands` | done | `classifyIndoor` (≤18 / ≤24 / ≤28), `classifyOutdoor` (≤0 / ≤15 / ≤28), `classifyBattery` (≤20 / ≤40 / ≤60), each band carrying `progress` from `bandProgress(t, from, to)` with the old divisors (8/4/8 indoor, 15/13/12 outdoor); `signalBarsFromRssi` (−50/−60/−70/−80 → 4..0) and `signalBarsFromStrength` (clamp 0..4); `legacyTemperature()` translates the old `-1.0f` sentinel to `kNoTemperature` at a boundary, never inside a model. `EnvironmentInfoScreen::getTemperatureColor` and `DeviceInfoScreen::getBatteryColor` / `getWifiStrengthBars` now call the model. **One visible change:** the WiFi label renders 0..4 bars; the old table stopped at three, so the strongest signal was drawn as three bars. | `test_bands`: 7 cases |
| MDL-08 band tests | done | Inclusive boundaries at every threshold; a −20..50 °C sweep in 0.1 °C steps compares band + progress through the model palette against the old `getTemperatureColor` copied verbatim: 0 mismatches over 1,402 points; `kNoTemperature`, `legacyTemperature(-1.0f)`, NaN and −300 classify Invalid; `classifyBattery(-1.0f)` is Invalid, never Critical; 10,000 classifications allocate nothing. Note `classifyOutdoor(-1.0f)` is Freezing: −1 °C is a real outdoor reading, which is why the translation is explicit. | green under ASan/UBSan |
| MDL-09 `ColorMath` | done | `hsvToRgb` / `channelFromRgb` moved bit-for-bit out of `LightController` (member deleted); `lerpChannel` is the old view lerp kept literally; `indoorTemperatureRgb` / `outdoorTemperatureRgb` / `batteryRgb` are the palettes, `Rgb` is a plain triple the views convert with `lv_color_make`. Both light call sites documented in the header: the on-screen swatch uses value 1.0, the MQTT publish uses value = level/100 (brightness folded into the colour). | `test_domain_helpers`: 361 hues × 21 levels identical to the old function, hue 1.0 wrap included |
| MDL-10 `DateFormat` | done | `formatDate(const char*, DateText&)` over `FixedString<8>`; digit guard kept; the day keeps at least one digit; `monthName(1..12)` else `---`. The old `std::stoi` could throw on a 10+ digit month; the port bounds the parse and shows `---`. `EnvironmentInfoScreen::formatDate` is a three-line call; `getMonthName` deleted. | `2026/02/07` → `7 FEB`, `garbage` → `-- ---`, month 0 / 13 → `7 ---`, 10-digit day truncates to the capacity |
| MDL-11 helper tests | done | 6 cases, all inside `NO_HEAP_SCOPE()` where the render path would run: malformed, empty, null, one slash, empty month, empty day, non-digit fields, month 0 and 13. | green |
| MDL-12 `RateLimit` | done | `RateLimiter(intervalMs)` (`markChanged` / `shouldPublish(now)` / `flush()`, first publish never delayed), `Debouncer(settleMs)` (`poke(now)` / `settled(now)` fires once per burst), `EchoSuppressor(windowMs)` (`published(value, now)` / `isEcho(value, now)`). No clock inside: every method takes `nowMs`. Not yet wired into the controllers (WP1.4 does that with the adapters). | `test_ratelimit`: 5 cases |
| MDL-13 rate-limit tests | done | Denies at n−1 ms, allows at n; a 100-change burst inside one window collapses to zero fires with the last value still pending, and `flush()` emits it; the debouncer fires exactly once 400 ms after the last poke; the echo window is inclusive at the boundary; 10,000 iterations allocate nothing. | green |
| PERI-03 `PowerRules` | done | The 13-point SoC curve moved to `model/PowerRules` (`socPercentFromVoltage`, `isValidCellVoltage`); `BatteryHandler::voltageToPercentage` deleted, both call sites (ADC and PMU fallback) call the model. An all-12-samples-failed ADC read now returns −1 and the handler reports both voltage and percentage invalid instead of re-using the filtered voltage as fresh; the filter restarts on the next good sample. `battery_handler` gained `REQUIRES model` as a bridge edge; it goes when `PowerModel` (MDL-25, WP1.3) owns the percentage and the adapter feeds raw voltage. | `test_power_rules`: 13 knees, 3 midpoints, both clamps, sentinels carried not zeroed, zero allocations. The on-device percentages are unchanged by construction (same arithmetic, same table). |
| APP-21 `CommandParsers` | done | `cmdparse::parseInt`, `splitColon`, `parseConnectWifi`, `parseConfigureMqttServer` (all five arities, `password == "-"` reuse), `parseStaticIp`; `CommandHandler` keeps the side effects and calls the leaf. Compiled on the host as its own static library, which is the proof that it is ESP-free. Two tightenings: `parseInt` rejects leading whitespace (`strtol` used to skip it), and the `mqtt://` / `mqtts://` re-glue now also applies after the `URL:` keyword (`URL:mqtts://host:user:pass` used to fail with `invalid_port`). | `test_command_parsers`: 8 cases, one per arity plus rejections (`setpoint:abc` shape, `Home:` trailing empty password rejected as before) |
| PCKNOB-06 PC protocol | done | `esp-idf/PC/src/pc_media_protocol.py`: sender framing (`split_sender` / `is_self` / `with_sender`), `parse_control` (play / pause / forward / rewind), `parse_setpoint` (`setpoint:` and `setpoint=`), `is_state_request`, `format_state` / `parse_state`, `parse_serial_line`. `mqtt_handler.py` and `sound_controller_app.py` route through it. Deltas: a setpoint outside 0..100 is rejected instead of clamped (matches APP-01), unknown control words are dropped in the handler instead of being queued and ignored, self-detection compares the sender id exactly instead of by substring. `pytest` was installed for the WSL build user (`pip install --user pytest`, 9.1.1). | `esp-idf/PC/tests/test_pc_media_protocol.py`: 16 cases green under `python3 -m pytest esp-idf/PC/tests` and `python3 -m unittest discover -s esp-idf/PC/tests`, with no pywin32 / pycaw / paho on the machine (asserted). `py_compile` clean on the three edited files. "`play` still works inside the app" is a bench item: the Windows host in this session has no Python, so the GUI was not launched. |

Deviations from the table: PCKNOB-06 lists PCKNOB-01 (retire `QNOB/PC`) as a dependency; the
owner keeps the stale copies, so the work was done against the canonical `esp-idf/PC` only.

Host suite after WP1.2: 8 executables, 50 cases, all green with `-fsanitize=address,undefined`;
purity guard clean; `bash run-host-tests.sh` from `screen/SW/ESP`.

On-device (all three variants built clean after WP1.2, working tree on top of `2e21074`):
`libmodel.a` now links because the views call it: 2,520 B of flash on the 1.75 (2,342 code +
178 rodata), 2,524 B on the 1.28, 3,854 B on the C6 (RISC-V code density plus `.eh_frame`);
0 IRAM and 0 DIRAM everywhere, so the S3 IRAM figure (16,383/16,384) cannot have moved and
MDL-01 stays provisional until APP-06 instantiates the models. `qnob-screen.bin`: 1.75
1,588,048 B (0x183B50, +736 B against the WP1.1 build of 1,587,312 B, the model code replacing
the view copies almost one for one); 1.28 1,546,256 B (0x179810); C6 1,733,504 B (0x1A7380);
47% or more of the app partition free on each. `battery_handler`'s new `REQUIRES model`
compiles and links on all three. The component graph still reports the two Phase-0 cycles.
Bench checks pending on the 1.75: four-bar WiFi label at strong signal, the date label after a
time sync, light swatch and published LED colour unchanged, and `play` from the PC app.

## WP1.3 The eight models

Started 2026-09-12 after WP1.2, on the owner's go-ahead. Additive: nothing in the firmware
constructs an `AppModel` yet (APP-06, WP1.4), so device behaviour is unchanged; the models
compile into `libmodel.a` on all three variants and are pinned by nine new host suites.

**Shape shared by all eight** (`model/ModelBase.h`): a plain-data `State` struct with defaults,
setters that mark the model dirty, and one `commit()` per drain that notifies subscribers only
when the state differs from the last notified copy (an equal set notifies nobody) and republishes
the seqlock snapshot other tasks read (D2). Subscriptions are the D3 function-pointer arrays,
eight per model, and are sealed by `AppModel::wireSubscriptions()`. Each model owns its `Field`
enum, the D18 record class per field (`classOf`), a `make()` factory for producers, and
`applyUpdate()` for the drain. Strings never cross the 16-byte queue: `setTrack()`,
`setTargetLabel()` and `setLegacyDateTime()` are display-task calls; the transport for Spotify
titles is decided with MEDIA-04/05 (residual, below).

| Task | Status | What changed | Verification |
|---|---|---|---|
| MDL-14 `MediaModel` | done | `activeTarget` (PC, the only target that exists), `volume` (50, SoundController's default), `isPlaying`, `stateSynced`, `knobBinding`, `track` (title/artist/album `FixedString<64>`, `id` `<24>`), `positionMs`/`durationMs`, `targetLabel`; publish bookkeeping is a 25 ms `RateLimiter` plus a 1.5 s `EchoSuppressor` (the pattern SoundController hand-rolled). Switching target forgets everything learned about the old one. | `sizeof(MediaModel)` 1,112 B on the 64-bit host (three state copies + 8 subscriber slots); an equal set notifies nobody |
| UIX-10 amendments | done | `availability[MediaTargetId]`, two slots with per-target legal states (Spotify OK/NO_TOKEN/NO_PREMIUM/OFFLINE/SYNCING, PC OK/PC_OFFLINE/NO_PLAYER/NO_MQTT/STALE; a state from the wrong slot is refused); `pendingSeekPercent` with -1 = none; `positionSampledAtMs` and `estimatedPositionMs(now)` extrapolating only while playing; `seekResolution` None/Coarse(10 %)/Fine(1 %) with `seekStepPercent` and `canSeek()` gated on a known duration (losing the duration drops SEEK back to VOLUME); `lastStateMs` with staleness at exactly 10 s; `track.id`; `ClimateModel::indoorSampledAtMs` with the 120 s gate. | `test_media_amendments`: 7 cases |
| MDL-15 media tests | done | Clamp, out-of-range rejection (the -1 an empty payload decodes to is refused: it used to mute), echo suppression inclusive at the window, one publish per 25 ms with a leave-screen flush, truncation to capacity, target switch, the two slots independent, 1,000 notify cycles with zero allocations. | `test_media_model`: 9 cases |
| MDL-16 `LightModel` | done | `brightness` 0..100 only (`setpoint` and `waterLevel` do not exist), `hue`/`saturation` clamped to 0..1, `LedMode` with `cycleMode()`; the single wire-scale rule (NET-22) as `wireFromPercent` (percent × 255 / 100, what the controller always published: 50 → 127) and `percentFromWire` (rounded), exact round trip for all 101 values; `publishedRgb()` (brightness folded in) and `swatchRgb()` (full value) name the two call sites. Publish slot 25 ms, CTRL-04's measured value rather than MDL-12's 10 ms estimate. | `test_light_model`: 6 cases |
| MDL-17 light tests | done | 0/100 clamps, 0..255 inbound, hue at the `i % 6` wrap (1.0 draws as 0.0), the LedMode cycle's first-ever execution, one publish per window, zero allocations. | green |
| MDL-18 `ClimateModel` (D4) | done | `targetTemp` (21.0 default) clamped 16..30 in 0.5 steps with `targetBand`/`targetProgress` tracked, humidity and pressure with validity (a malformed value marks the field invalid and is never stored), `indoorSampledAtMs`, `publishPending` plus a 400 ms `Debouncer` for the settled target. The temperatures themselves stay in `EnvironmentModel`. | `test_climate_model`: 7 cases |
| MDL-19 climate tests | done | NaN and both infinities rejected, the clamp at both ends, band tracking through all four bands, validity gating, settle-once, zero allocations. | green |
| MDL-20 `EnvironmentModel` | done | Civil date/time/day from a UTC epoch plus an offset (Hinnant's civil-from-days, no `<ctime>`, so the poller posts numbers), mutating only when the minute changes; `date` "YYYY/MM/DD" feeds the WP1.2 `formatDate`; a legacy string path for the shadow phase that truncates to capacity; both temperatures with validity bits and bands; the old 0.5 °C hysteresis, except that the first write is always admitted. | `test_environment_model`: 8 cases |
| MDL-21 environment tests | done | 0.4 suppressed, 0.5 suppressed (as before), 0.6 admitted; a true 18.0 °C is admitted on first write instead of being discarded as "unchanged"; 2026-02-07 12:34 UTC at UTC+3 renders `2026/02/07`, `15:34`, `SAT`, `7 FEB`; a 40-character date truncates to 10; bands stay continuous across a 15..30 sweep; 10,000 updates allocate nothing. | green |
| MDL-22 `ConnectivityModel` | done | `wifiConnected`, `internetAvailable`, `mqttConnected`, `mqttLightConfigured`, `bars` 0..4, `rssiDbm`; `setRssi` maps with the driver's thresholds; strength 5 rejected. The model cascades nothing: each flag is what its poller reported. | `test_connectivity_model`: 4 cases |
| MDL-23 connectivity tests | done | `mqtt=false` leaves `wifi` alone, 0..4 accepted and 5 refused, five RSSI inputs give five distinct bar values (the top bar is reachable), zero allocations. | green |
| MDL-24 `PowerModel` | done | `percent`/`voltage`/`currentMa` each with a validity bit (the -1.0f sentinel clears the field and yields `BatteryBand::Invalid`), `connected` and `presenceKnown` as separate facts, `charging`, `derivePercentFromVoltage()` over `PowerRules`. | appended to `test_power_rules` (now 9 cases) |
| MDL-25 power tests | done | Sentinel → `percentValid` false and no band; a known-absent cell is `presenceKnown` true, `connected` false, and never charging; percent derived from 3.85 V is 55; zero allocations. | green |
| MDL-26 `NavigationModel` + `AppScreen` | done | `AppScreen` {Boot, Info, DeviceInfo, ModeWheel, Media, Light, Climate}: no SLEEP (D8) and none of the old `INITIALIZATION`/`SOUND`/`HOME`/`CALIBRATE_ORIENTATION`; `KnobMode` {LIGHT, CLIMATE, MEDIA}; a transition table (`canTransition`) with an entry for every screen; `requestScreen`/`takeRequest` as a single-consumption intent where the latest unconsumed request wins; one `lastActivityMs` that never rewinds; `initProgressPct` monotonic and completed on leaving Boot; the wheel wraps. | `test_navigation_model`: 8 cases |
| MDL-27 navigation tests | done | `request_consumed_once` (a flag implementation would still read the screen on the second take), latest-wins, no transition to a dropped or impossible screen, every screen has an exit, one activity clock, no backward ramp, zero allocations. | green |
| MDL-28 `KnobModel` (D5) | done | `detentCount` replica 0..100; haptic defaults 120 / 1.0 / 0.55 / 3.0 from the knob firmware, validated on write; `applyDelta()` for `+N`/`-N`; `burstToDelta()` for a coalesced `setpoint=N`: N − lastN clamped ±10, the replica records N, the overflow is counted; `stepsFor()` is the one UIX-11 accumulator (4 per wheel step, 3 per target toggle, 1 per volume or brightness unit, 1 per 0.5 °C, 1 or 3 per seek percent) reset on direction reversal and after 600 ms idle; `onEnter()` sets the context and requests a write of the mode's value; `takeWriteRequest`/`noteWriteAck`/`tick` with a 1 s ack timeout that assumes the write landed; link liveness 5 s from any inbound line (PCKNOB-17 adds ping/pong). **The leak fix:** while a write is outstanding, absolute bursts are stale relative to the old base and are dropped, counted in `droppedBursts`. | `test_knob_model`: 11 cases |
| MDL-29 knob tests | done | Defaults equal the firmware; net-zero deltas; from 40, `burstToDelta(43)` = +3; clamp at ±10 with the replica at N; `mode_switch_does_not_leak_setpoint` (80 in MEDIA, enter LIGHT at 20, a stale 80 burst yields 0, the ack moves the replica to 20, the next burst 23 yields +3); write timeout; clamp at both ends; link timeout at exactly 5 s; the accumulator per divisor with reversal and idle resets; haptic validation; 10,000 mixed operations allocate nothing. | green |
| MDL-30 `AppModel` | done | The eight models by value; `drainPendingMutations(now)` applies every record to its aggregate, ticks the knob, then `commitAll()` so each changed model notifies once per frame; `wireSubscriptions(fn, ctx)` runs the composition's wiring once and seals every model (a later subscribe returns false; a second call returns false); `unroutedRecords()` and `rejectedRecords()` counters for MDL-32. | `test_app_model`: 6 cases, incl. three coalesced volume records collapsing to one notify, and a 500-frame drain with subscribers allocating nothing |

Host suite after WP1.3: 17 executables, 120 cases, all green with ASan/UBSan; purity guard clean.

`sizeof` on the 64-bit host: `AppModel` 4,248 B (`MutationQueue` 1,128, `MediaModel` 1,112,
`EnvironmentModel` 400, `KnobModel` 392, `ClimateModel` 320, `PowerModel` 232, `LightModel` 224,
`NavigationModel` 224, `ConnectivityModel` 176). Each model carries three copies of its state
(working, last-notified, snapshot) plus eight 16-byte subscriber slots; on the 32-bit targets the
slots halve and the estimate is about 3.7 KB, inside the 4 KB MDL-01 budget with little room.
The device figure comes from `size-components` `.bss` once APP-06 instantiates the root.

Residuals: (1) track title/artist/album transport from the network task to the display task is
not a queue record; MEDIA-04/05 decide it (a fixed string mailbox with a generation counter is
the candidate). (2) `battery_handler → model` bridge edge from WP1.2 stays until PERI-06 feeds
`PowerModel` from the poller. (3) KnobModel's link timeout (5 s) is a placeholder until PCKNOB-17
gives the link a heartbeat. (4) The WP1.2 visible deltas were added to the section 8.2 P1b
allow-list.

On-device (all three variants built clean after WP1.3): the binaries are byte-identical to the
WP1.2 build (1.75 1,588,048 B; 1.28 1,546,256 B; C6 1,733,504 B) and the `libmodel.a` rows are
unchanged (2,520 / 2,524 / 3,854 B), because nothing references the eight model objects yet and
the linker leaves unreferenced archive members out. The sources compiled on both toolchains
(xtensa and RISC-V) under the firmware flags. The first real measurement of the model layer on
the device comes with APP-06, which constructs the root.

## WP1.4 Shadow population and ownership fixes

Started 2026-09-12 after WP1.3, on the owner's go-ahead and before `p0-fixed` exists (the P1b
comparison against it is still owed). This is the first package that changes behaviour: the
model root is live, the network task no longer blocks, the battery leaves the render loop, and
the four screen callbacks read the model. Every visible delta is on the section 8.2 P1b
allow-list.

**Shape.** `AppModel` is a global held by value in `main.cpp`; `adapters::model_host` installs
the queue's spinlock, sleep, log and drain-guard hooks and the clock, and the display task
registers itself as the single consumer with its first statement. Every existing data path that
reaches a screen also posts a `ModelUpdate` record; nothing outside `AppModel::apply()` calls a
model setter. Three pollers on the network task feed the connectivity, environment and power
models by push-on-change; the screens read the model through their existing callbacks (NET-11)
and keep their own fields until Phase 3.

| Task | Status | What changed | Verification |
|---|---|---|---|
| APP-06 root | done | `static model::AppModel appModel` in `main.cpp`; `adapters/ModelHost` installs the hooks (portMUX critical section, `vTaskDelay` retry sleep, `ESP_LOGW` for overflow lines, `xTaskGetCurrentTaskHandle()` guard, `esp_timer` clock); `drainPendingMutations()` is the first thing the display frame does; `wireSubscriptions()` seals before the tasks start. `main` already required `model` and `adapters` (BLD-41). | builds on all three variants; with `CONFIG_QNOB_MODEL_TRACE=y` one `MODEL:` drain line plus one line per changed model |
| MDL-31 shadow writes | done | The MQTT lambda posts `IndoorTemp` (float) and `IndoorSampled` for `qnob/indoor/temperature` and `Brightness` for the light topic, from the MQTT task, through the queue; `DisplayController`, `SoundController` and `LightController` take a queue pointer and post records (below); the four screen callbacks read model state. `grep` for a model setter outside `AppModel::apply` finds none. | code review; the trace on the bench is the runtime check |
| MDL-32 `ModelTrace` | done | `model/TraceHooks` (hooks, enable flag, write and off-task counters, task name, log sink) and `model/ModelTrace` (one fixed-buffer formatter per state). Compile-gated by `QNOB_MODEL_TRACE`, which `components/model/CMakeLists.txt` defines from `CONFIG_QNOB_MODEL_TRACE` (a new Kconfig bool, default n); the purity guard now checks Kconfig symbols in sources only, so the CMake glue may read it. `ModelBase::mutate()` calls `trace::noteWrite()` under the gate: a write from any task but the consumer is counted. `AppModel` logs `drain task=<name> n=<records> commit=<models> unrouted= rejected= offtask=` and the changed models. The host build compiles with the trace on. | `test_model_trace`: the counter follows the guard, a drain logs 1 + N lines, formatters fit and allocate nothing |
| INPT-05 knob shadow | done | `DisplayController::postKnobToken`: `+`/`-` → `KnobModel::Field::Delta` ±1, `setpoint=N` (the burst the command grammar drops today) → `Field::Burst`, anything else → `LinkActivity`. Navigation shadow in the same class: `ActiveScreen` on every mode change (SLEEP and CALIBRATE post nothing, D8), `Activity` on every touch or knob event, `InitProgress` on the ramp, `ModeStep` and `Mode` on the wheel. | 20 detents in SOUND move `detentCount` 1:1 (bench, trace) |
| CTRL-07 media shadow | done | `SoundController::postMedia`: local changes post `VolumeLocal` (no echo check), inbound `setpoint:` and `state:` post `Volume` and `IsPlaying`, bare `playing`/`paused` post `IsPlaying`. | trace vs the arc and the state label (bench) |
| CTRL-08 light shadow | done | `LightController` posts `Brightness` (the field is `brightness`; `waterLevel` stays a view word), `Hue` (float) during a drag, `Mode` on a mode change, and `Brightness` on the inbound MQTT value. | trace vs the fill and the hue (bench) |
| NET-09 one internet owner | done | `InternetHandler` owns `internetAvailable` (an atomic); `WiFiManager` lost its copy, `checkInternetConnectivity()` and the "reconnected → re-check" branch; `MQTTManager` takes `setInternetPredicate()` and falls back to the link state without one. The probe runs immediately when the link comes up, then every 30 s. | MQTT connects as soon as the first probe answers (bench: <10 s after IP) |
| NET-10 no cross-task strings | done | `TimeHandler` keeps date/time/day in fixed character arrays refreshed only on the network task; its getters are `const` and return copies. `WiFiManager` keeps SSID and IP in fixed arrays written under a spinlock by the event task; getters return copies. The C shim's static `std::string` is a fixed buffer too. | code review; the 30-minute soak is a bench item |
| NET-06 `ConnectivityPoller` | done | `adapters/ConnectivityPoller`: a pure core (`poll(sample, now, queue)`) plus the driver-facing wrapper under `ESP_PLATFORM`. Posts each of wifi / internet / mqtt / light-configured on change and `Rssi` only when the mapped bars change; the mapping is `model::signalBarsFromRssi`, not the driver's. | `test_pollers`: 5 records on the first poll, 0 while steady, 0 for RSSI jitter inside a bar, all five bar values reachable |
| NET-07 `EnvironmentPoller` + config | done | Posts `Epoch` (UTC seconds + offset minutes) only when the local minute changes and `OutdoorTemp` when the weather reading changes; `EnvironmentModel` derives the civil date. Timezone and NTP servers left the literals: `CONFIG_QNOB_UTC_OFFSET_MINUTES` (default 180), `CONFIG_QNOB_NTP_SERVER_1/2`. **DST decision (section 9 Q28):** no DST handling; the offset is fixed from config. Türkiye has used permanent UTC+3 since 2016, so the bench is correct; an NVS override is a later row. The colon blink stays in the view at 1 Hz. | `test_pollers`: nine minute boundaries in ten simulated minutes at the 100 ms cadence, no string ever posted |
| NET-31 poller tests | done | `test_pollers.cpp` over sample structs, a real `MutationQueue` and an `AppModel`, no ESP header in the translation unit; `adapters/sources.cmake` compiles the same core files on the host. | 5 cases green |
| PERI-05 real presence | done | `BatteryHandler::isPresenceKnown()` is true only when the AXP2101 answered the presence bit; `isBatteryConnected()` is that answer, never "telemetry present". The ADC boards report presence unknown and the DeviceInfo screen shows its `est` wording (the allow-listed delta). | C6 says estimate; `[AXP2101]` flips on cell removal (bench) |
| PERI-06 `BatteryPoller` | done | Posts `Percent` and `Voltage` (floats, the −1 sentinel passed through so the model marks the reading invalid), `PresenceKnown`, `Connected` and `Charging` on change. | `test_pollers`: sentinel → `percentValid` false, presence known and not connected |
| PERI-07 off the render loop | done | `batteryHandler->analyze()` runs on the network task before the poller, outside the WiFi branch, so the reading refreshes with the link down. | trace attributes the sample to NetworkTask (bench) |
| PERI-08 dead telemetry | done, one deviation | `currentMilliamps` and `lastRawSpread` deleted. `isCharging()` is kept: the WBS row calls it never-populated, but the AXP2101 path does populate it from STATUS2 and `PowerModel.charging` consumes it. | build clean; percentage arithmetic untouched |
| NET-11 callbacks on the model | done | Date/time/day, outdoor temperature, network status and battery status come from `appModel.<model>().state()` on the display task. The pollers run every 100 ms, so the visible refresh rates are unchanged. | bench: INFO and DEVICE_INFO identical to before |
| NET-28 weather key out of the header | done | `CONFIG_QNOB_WEATHER_API_KEY` / `_CITY` / `_COUNTRY` (Kconfig; the key defaults to empty and belongs in `sdkconfig.defaults.local`, documented in the example overlay); the request is `https://` with `esp_crt_bundle_attach`; `weather_handler` requires `mbedtls`. The leaked literal is gone from the tree; the rotation itself (PCKNOB-02b) is still the owner's. | an empty key logs "not configured" and performs no fetch (code path); 200 over TLS is a bench item |
| NET-29 instance-owned buffer | done | The 2 KB buffer, its length and a truncation flag are members; the event handler receives `this` through `user_data`; chunked bodies are appended like any other; a truncated body is logged and never parsed. | two back-to-back fetches are independent by construction |
| NET-30 no blocking on the network task | done | WiFi: `connectToWiFi()` requests a sequence that `update()` runs one candidate per 15 s, reacting to the driver's events; nothing waits on an event group. NTP: `startSync()` returns at once and `update()` observes validity. HTTP: the weather fetch is `is_async` and `service()` advances it per iteration with a 10 s deadline. Internet probe: a non-blocking TCP connect polled with `select` and a 3 s deadline. The `connectWifi` command from the command task only sets a flag. | bench: no network-task iteration over 200 ms; MQTT back within 10 s after an AP power cycle |
| PCKNOB-04 link contract | done | `components/knob_controller/knob_link_protocol.md`: framing, every v1 token with its producer and consumer cited to `file:line`, the v2 form, `=` versus `:`, and the finding that the knob's `Updated setpoint` ack goes to USB, never to the link. | every v2 token has one producer and one consumer |
| PCKNOB-05 unread `last_setPoint` | done, not committed | Deleted from `HapticController3.ino` and `FOC.ino` in the working tree. The Motor Driver folder is in the QNOB repository, whose working tree carries the owner's uncommitted hardware edits, so this change stays uncommitted for the owner to include; the sketch was not compiled here (no Arduino toolchain). | `grep last_setPoint` empty |

Host suite after WP1.4: 19 executables, 128 cases, all green with ASan/UBSan and the trace
compiled in; purity guard clean and re-proven against an injected Kconfig token.

Kconfig additions (`main/Kconfig.projbuild`): `QNOB_MODEL_TRACE`, `QNOB_UTC_OFFSET_MINUTES`,
`QNOB_NTP_SERVER_1`, `QNOB_NTP_SERVER_2`, `QNOB_WEATHER_API_KEY`, `QNOB_WEATHER_CITY`,
`QNOB_WEATHER_COUNTRY`.

Residuals: (1) the indoor temperature still reaches `EnvironmentInfoScreen` by a direct call
from the MQTT task (a pre-existing cross-task write into the screen) beside the record; Phase 3
removes it with the screen. (2) The weather key has no NVS override yet. (3) The knob write path
(`KnobModel::takeWriteRequest`) is not wired to the UART until PCKNOB-15. (4) `WiFiManager`'s
scan and AP-STA surface is still present for NET-27.

On-device (all three variants built clean after WP1.4; the Kconfig change forced full rebuilds):
the whole model links now. `libmodel.a` is 17,951 B of flash on the 1.75 (17,967 B on the 1.28,
26,358 B on the C6), `libadapters.a` 2,275 B; the model root is the 3,773 B `.bss` of `libmain.a`
on the 1.75 (3,769 B on the C6), inside the 4 KB MDL-01 budget. Binaries: 1.75 1,609,488 B
(0x188F10, +21,440 B against WP1.3, 51 % of the app partition free); 1.28 1,567,776 B (0x17EC20,
+21,520 B); C6 1,762,416 B (0x1AE470, +28,912 B, 46 % free). DIRAM on the 1.75 208,883 B used
(61.1 %), IRAM unchanged at 16,383 / 16,384; C6 DIRAM 230,819 B (51.1 %). No new first-party
warning beyond the pre-existing missing-field-initializer on the scan config. Bench items pending
on the 1.75: the trace walk (knob + INFO) with `CONFIG_QNOB_MODEL_TRACE=y` and the off-task
counter reading 0, MQTT up within 10 s of IP, recovery after an AP power cycle, the weather 200
over TLS with a rotated key, and the P1b comparison against `p0-fixed` once that tag exists.
