# Integration log: main merged into the refactor (2026-10-23)

Handover item 3 ("the branch integration"), done as one merge commit (`240ee2e`, parents
`680c630` = `refactor/mvc-phase3` and `1df1ecf` = `origin/main`) on `claude/blissful-mayer-kxvyad`.
Nothing on the remote was rewritten; `codex/multi-board-support`, `main` and `refactor/mvc-phase3`
are untouched and are now all ancestors of this branch, which is 41 commits ahead of `main` and 0
behind.

## 1. What was merged, and the base

The handover framed the problem as 14 commits on `codex/multi-board-support`. Those 14 had already
been merged into `main` (PR #1), and `main` carries 109 more after them: the folder restructure,
Bluetooth (HID, battery service, BLE serial line), the connectivity refactor (T-04..T-20), the setup
portal, pairing and auth (A1, A3..A7), the boot guard, the Windows companion app (B1..B4, C1..C5) and
the protocol spec (D1). The integration target was therefore `main`, not the codex branch.

The base decision of 2026-09-13 (`phase1-log.md`) stands: this line is the code base, and main's
screen layer was ported file by file in `dbd63e3`. The merge confirms the port: `InitializationScreen`,
`ModeController`, `PlaceholderPageScreen`, `DeviceInfoScreen.h` and the three fonts were byte-identical
to main at the port, and the other screen and display files differ only by the model lifts and the
WP1.4 posts. So every UI and display file keeps the refactor's version, and what main added outside
the screens is brought in.

## 2. Layout

Main's restructure (PR #4, #25) is adopted: firmware under `src/esp-idf/`, scripts under `scripts/`
(the host-test runners move there), datasheets and `variant_overview.csv` under
`documents/Hardware Ref/`, logos under `assets/`, the companion under `Companions/Windows`.
Out-of-tree builds, generated sdkconfigs, host-test builds and bench logs go to `helpers/`; published
images to `binaries/` (OTA images in `binaries/ota/`). Both are ignored.

**One manual step on every working copy:** the untracked bench overlay does not move with checkout.
Move `esp-idf/sdkconfig.defaults.local` to `src/esp-idf/sdkconfig.defaults.local`. The old
`esp-idf/` and `Hardware Ref/build_*` directories left on disk are only ignored build output.

Not carried from main: the committed build output (`src/esp-idf/build/`, 2,329 files;
`build_s3_amoled175/`, 31 files; a root `sdkconfig.esp32s3_amoled175`), which BLD-02 removes on this
line anyway, and `scripts/monitor_out.txt`, a captured monitor log. Main's deletions of `arduino/`,
`official examples-*`, `esp-idf/screen-test/` and `issues/` apply; BLD-37 had kept `screen-test/`
as a bring-up app without knowing main had deleted it. It is recoverable from `680c630`.

## 3. Resolution by file

| Area | Resolution |
|---|---|
| `bluetooth_manager`, `boot_guard`, `command_envelope`, `connectivity_manager`, `mdns_advertiser` | main, verbatim (new components) |
| `ota_manager`, `storage/NVSManager.*`, `wifi_manager/SetupPortal.*`, `portal/*.html`, `ui/screens/SafeModeScreen.*`, `main/idf_component.yml` (adds `espressif/mdns`) | main, verbatim (the refactor had not changed them) |
| `wifi_manager/WiFiManager.*` | main's ConnectivityManager design (APSTA, portal, scan then connect, enable flags), with NET-09 and NET-10 re-applied (section 4) |
| `command_handler/*` | merged (section 4) |
| `internet_handler/InternetHandler.*` | the refactor's non-blocking handler, with main's NVS weather settings; no literal key |
| `main/main.cpp`, `main/CMakeLists.txt` | the refactor's composition with main's boot guard, safe mode, Bluetooth, ConnectivityManager, OTA config, mDNS and the portal and BLE bridges (section 4) |
| `display/DisplayController.*` | the refactor's, plus `requestBrightnessPercent()` for main's `setBrightness` command |
| `board_hal/hal_config.h` | the refactor's, plus main's `QNOB_OTA_VARIANT_ID` per variant; main's `TOUCH_SWAP_XY`/`TOUCH_MIRROR_*` are dropped (the Kconfig transform replaced them) |
| `sleep_handler/*` | the refactor's (section 4) |
| `time_handler/*`, the UI, the screens, `touch_panel/*`, `ui/LvglDisplay.*`, the display backends | the refactor's (the port already holds main's screen work) |
| LovyanGFX, `LGFX_Config.hpp`, `Arc`, `Buttons`, `AnimationHelper`, `InfoScreen`, `InitializationScreen`, `DeviceInfoScreen`, `Sound`/`Light`/`MediaController` | stay deleted (retired by the refactor); `MediaController` lives on as `local_audio/LocalAudio` |
| `sdkconfig.defaults` | the refactor's, plus main's Bluetooth block, `ESP_SYSTEM_EVENT_TASK_STACK_SIZE=8192`, `FREERTOS_TIMER_TASK_STACK_DEPTH=4096` and its two draw caches; D11 stays (QR code off, refresh period 30 ms) |
| dependency locks | the refactor's per-target `dependencies.lock.<target>` (BLD-04); main's bare `dependencies.lock` (IDF 5.5.1) is not carried |
| `scripts/build-variant.{sh,ps1}` | main's paths and `PROJECT_VER`/`ESP_IDF_EXPORT` options with the refactor's lock policy, local overlay and component-graph report |
| `Companions/`, `.github/workflows/companion.yml`, `docs/protocol.md`, `documents/`, `assets/` | main, verbatim |
| `src/esp-idf/PC/` | the refactor's (credential purge, tests); main only moved it |

## 4. Where the two sides disagreed

**WiFiManager.** Main moved every `esp_wifi_*` call behind ConnectivityManager and made connect
event-driven (T-04..T-20); the refactor had made its own non-blocking connect (NET-30). Main's design
is kept because it carries the portal, APSTA, scan-then-connect and the Bluetooth coexistence.
Re-applied on top of it:
- NET-09: `checkInternetConnectivity()` did a blocking DNS lookup and a blocking TCP connect on the
  network task at every STA connect, a second owner of "internet available". It is removed with
  `isInternetAvailable()`; nothing in the merged tree called it, and InternetHandler stays the owner.
- NET-10: `current_ssid` and `current_ip` were `std::string` members written on the event task and
  read from the network, command and HTTP tasks. `getSSID()`/`getIPAddress()` now copy the
  ConnectivityManager snapshot (fixed arrays under its seqlock); the SSID between `STA_CONNECTED` and
  `GOT_IP` is an event-task-only buffer.

**CommandHandler, the trust model.** The refactor's APP-03: serial and the knob are trusted, MQTT runs
only an allowlist. Main's A6: a JSON envelope must carry the pairing token (except `pair`, `ping`,
`unpair`); a plain line over BLE or the portal ran anything. Merged sources:

| Source | Runs |
|---|---|
| Serial (USB console) | everything |
| Portal (the setup portal's own forms: `factoryReset`, `stopPortal`, `portalWifiCredential`) | everything, as on main |
| Remote: a JSON envelope over BLE with a valid token, or `pair`/`ping`/`unpair` | everything, as on main |
| RemoteLine: a plain line over BLE, no envelope | the MQTT allowlist only (was everything on main) |
| MQTT | the allowlist, unchanged; none of main's new commands was added to it |

The companion only sends JSON envelopes (and the plain serial terminal goes over USB), so this closes
main's unauthenticated plain-line path without breaking it. Main's commands keep their behaviour
except: `setBrightness` and `screen` go through display-task requests (the refactor's threading rule;
`setBrightnessPercent()` writes the panel), `screen:calibrate*` is refused like
`calibrateOrientation`, and `otaInfo` answers JSON inside an envelope (the companion parses it) and
the old `otaInfo:version=...` line over MQTT (the PC app parses that). `+`/`-` are not carried: the
knob belongs to the InputRouter. `connectWifi`, `setDeviceName` and `configureMQTTServer` accept
both the colon form (the host-tested `cmdparse` grammar) and main's JSON params. One dispatch mutex
now covers all four entry tasks, the serial console included.

**main.cpp.** Boot guard first; Emergency halts; a safe-mode level takes `runSafeMode()`: the
safe-mode face on the refactor's esp_lcd/LVGL stack (Rescue, OfflineRecovery), AP and portal only,
no screens, MQTT, media, Bluetooth or command handler, as on main. The normal path is the refactor's
composition with main's steps in main's order: pairing token, the 3 s PWR_KEY recovery hold,
Bluetooth before WiFi (coexistence), `ConnectivityManager::begin()`, OTA config from NVS, mDNS (up on
the AP, restarted on every STA edge), the portal and BLE-serial bridges into CommandHandler, the 15 s
health timer last. Three changes from main's code:
- the recovery hold now waits for the key's release: the display task starts within a frame here
  (on main it started seconds later) and restarts the chip on a pressed PWR_KEY;
- the BLE battery level is updated on the network task after `analyze()` (PERI-07 moved battery
  acquisition off the render loop; main did it every display frame);
- the PC page's play/pause is a BLE HID media key when a host is paired, otherwise the media target
  (MQTT), so both of main's and the refactor's paths still work.

**SleepHandler.** Both sides fixed the same 120 MHz retry flood; the refactor's fix (valid
frequency steps, one attempt per idle period) and its USB-host hold are kept. Main's
`enterLightSleep(1000)` slices are dropped: each wake resets the activity time, so the board slept
one second in every two minutes and never reached stage 3. The USB-host hold covers main's reason
for the slices (serial/JTAG recovery after a wake).

**Time placeholders.** Main pre-filled the clock with `1994/03/11 04:30 FRI`; the model's
`----/--/--`, `--:--`, `---` stay (EnvironmentModel's "not known yet").

**InternetHandler.** Main read the weather key, city and country from NVS and fell back to the
leaked key. Now: NVS when set, else Kconfig (`CONFIG_QNOB_WEATHER_*`, key empty by default).

## 5. Credentials

The merged index carries none of the three burned values. Main's tree still had the weather key in
`InternetHandler.cpp` (the fallback), `WeatherHandler.h` and the committed build output, the bench
PSK in `sdkconfig.defaults.esp32c6`, and the HiveMQ host, user and password in three PC-app files; all
resolve to the refactor's purged versions or are not carried. Both handover greps are empty, and so
is a wider gate run before each commit of this work (weather key, PSK value, broker host, user and
password, derived from history at run time). `scripts/monitor_out.txt`, not carried, held the
bench SSID (not the PSK).

## 6. Verification

| Check | Result |
|---|---|
| `bash scripts/run-host-tests.sh` | 34/34 suites, ASan/UBSan, `-Werror`, purity guard ok |
| `python3 tools/check-include-edges.py src/esp-idf` | 30 components (25 + main's 5), 0 undeclared include edges |
| `node tools/wbs-lint.mjs` | 336 ids / 1022 h, clean |
| `src/esp-idf/PC` pytest | 16 passed |
| host `-fsyntax-only -Wall -Wextra` of every translation unit the merge touched or whose headers it changed (11: `main.cpp`, `CommandHandler`, `WiFiManager`, `SetupPortal`, `InternetHandler`, `DisplayController`, `ConnectivityPoller`, `EnvironmentPoller`, `MQTTManager`, `OTAManager`, `MdnsAdvertiser`), S3 1.75 and C6 configurations | 0 errors, 0 warnings; real first-party headers and LVGL, hand-written stubs for the IDF headers; a planted typo in a `BluetoothManager` call is caught |
| ESP-IDF firmware build | **not run**: the environment's network policy blocks `dl.espressif.com` and `components.espressif.com`, and no bare-metal C++ toolchain is installable here |
| Bench | **not run** |

What the syntax check does not prove: that the stubbed IDF signatures match IDF (the IDF calls in the
merged code are main's, which built), linking, flash, IRAM and RAM fit, or behaviour.

## 7. Owed before this goes to main, in order

1. **First build of the merged tree**, all three variants. Open points it will answer:
   - IDF version. The refactor and its bench run IDF 5.3.2 in WSL; main's code was developed and
     built on IDF 5.5.1 (the lock, `scripts/build-current-amoled175.ps1`). The per-target locks
     still say 5.3.2.
   - The locks do not list `espressif/mdns` yet: the first build re-solves them (network needed);
     commit the regenerated `dependencies.lock.<target>`.
   - Bluetooth on the S3: phase 0 measured IRAM at 16,383 of 16,384 bytes. Main's BT build fit on
     the 1.75 under IDF 5.5.1; whether the refactor plus Bluedroid fits is unmeasured. Watch IRAM,
     the app slot (was 42 % free) and internal RAM (Bluedroid plus WiFi plus the display buffers).
2. **Bench, merged features** (`bench_runbook.md` section 4): boot guard, safe mode, portal,
   Bluetooth, mDNS and pairing, plus A1..A7, whose timings may move with the new WiFi connect path.
3. **WBS reconciliation** against what main already built: `OTA-01..07` against `ota_manager` and
   the release workflow; WP3.7 (PC protocol v2 and the PC app) against `docs/protocol.md` and
   `Companions/Windows`; the NET rows against ConnectivityManager; the DEVICE_INFO Bluetooth and
   software-update rows (the refactor's view has none; main's screen never drew them); APP-03 now
   covering the portal and BLE.

## 8. Observations about main, not changed here

- mDNS advertises `_qnob._tcp` on port 23456, but no TCP command server listens: A2's
  `CommandServer` (`ff56734`) is on `feat/A2-tcp-command-server` and never reached main.
- `pair` returns the pairing token to any caller and ignores the token the companion sends;
  `unpair` needs no auth and regenerates the token. The auth gate therefore stops a client that does
  not know to call `pair` first, and nothing more.
- The boot guard counts every boot that ends within 15 s as a crash, whatever the reset reason:
  three short boots (flash, RST, an RTS-reset capture) land in Rescue mode, and one boot past 15 s
  clears it.
- The companion's WiFi tab sends `connectWifi` without its parameters (its own comment says so).

## 9. Reviewing the merge

`git show --stat 240ee2e` lists everything. `git diff 240ee2e^1 240ee2e -- <path>` shows what main
brought to a file, and `git diff 240ee2e^2 240ee2e -- <path>` what the refactor and the adaptations
changed relative to main. For the adapted files, the second diff against main is the review:
`src/esp-idf/components/wifi_manager/WiFiManager.cpp` (NET-09/10 only),
`src/esp-idf/components/command_handler/CommandHandler.cpp`, `src/esp-idf/main/main.cpp`.
