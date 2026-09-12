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
