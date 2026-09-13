# QNOB MVC Refactor — Work Breakdown Structure (v2)

## 1. Scope, register and phase totals

Execution document extending `dev_notes/mvc_refactor_plan.md`. Target tree: `screen/SW/ESP/esp-idf` (`screen - Copy` is a March 2026 snapshot and is not a migration target). Live target ESP32-C6-Touch-AMOLED-1.43, single core, 512KB SRAM, no PSRAM, 466×466 AMOLED, ESP-IDF 5.3, LVGL 8.4, `LV_DISP_DEF_REFR_PERIOD=30` (≈33Hz, not 60).

Task IDs are the reader IDs from the 11-cluster sweep (`DISP- SCRN- CTRL- INPT- NET- APP- PERI- BLD- MDL- MEDIA- PCKNOB-`) plus `UIX-` for work introduced by the winning "Instrument" screen design and the design judges' blocking issues. Paths are relative to `esp-idf/` unless prefixed `PC/`, `Motor Driver/` or `dev_notes/`. Screen and UI work packages live in §4 and are not repeated in §3; every §4 task is Phase 3 except `UIX-01`/`UIX-02` (Phase 1 floor) and `UIX-10` (Phase 1 model), which are listed in §3.

**ID accounting.** The sweep produced 361 reader ids. 274 survive as rows in §3/§4; 87 are folded into a survivor and every one of them is named in the §2.1 merge register. 46 ids are new to this document (28 `UIX-`, plus `BLD-37…45`, `MDL-40/41`, `NET-31`, `PCKNOB-02b`, `DISP-28`, `SCRN-28`, `APP-27`). 274 + 44 = **318 distinct task ids**, and every id below appears exactly once.

**Estimate basis.** Hours are for one developer and **include the stated verification**; they exclude hardware bring-up, the §9 hardware answers, and re-work caused by them. No single task exceeds 8h; anything that did was split (`APP-16`/`APP-27`, `SCRN-28`/`SCRN-24`). Every verification with a repeat count of ten or more has that bench time inside its estimate.

**Hardware inventory the estimates assume.** C6-AMOLED-1.43 **and** the V11 knob on the bench. Verifications marked `[S3-1.75]`, `[S3-1.28]`, `[AXP2101]` or `[ES8311-capture]` need hardware the C6 does not have; if a board is absent the step is **deferred and recorded**, never silently skipped (§9 Q37). Build-only "three variants build" steps need no extra board.

| Phase | Scope | Tasks | Est h |
|---|---|---|---|
| 0 | Rotate secrets, blank the WiFi defaults, freeze the tree, measure, archive the graph, tag `p0-baseline`. **No behaviour change after the capture.** | 17 | 29.5 |
| 1 | WP1.0 floor (crashes, secrets, the four never-worked paths, tag `p0-fixed`); then `model`, `Observable`, D1/D18 queue, host harness, shadow population | 79 | 230.5 |
| 2 | `InputRouter` + `InputEvent`; all 21 polling sites; knob framing; knob ownership; `LocalAudio` move | 33 | 103.0 |
| 3 | Screen splits, the Instrument shell, MEDIA/CLIMATE, adapters, PC protocol v2, Spotify | 117 | 383.5 |
| 4 | `ScreenRouter` replaces `DisplayController`; power/sleep split; panel-backend purge | 29 | 76.5 |
| 5 | Transport out of views; `MqttModelBridge` owns every topic | 15 | 52.0 |
| 6 | `AppComposition`; ownership; task topology | 15 | 44.0 |
| 7 | CMake tightening, cycle proof, dead-code purge | 15 | 41.0 |
| **Total** | | **320** | **960.0** |

Per-phase figures are the row counts and hour sums of the tables in §3 and §4, stated per work package so the roll-up is checkable by hand; `BLD-39` scripts the check and `BLD-36` runs it in CI.

---

## 2. Global decisions

| # | Decision | Answer | Why |
|---|---|---|---|
| D1 | Cross-task model mutation | **Fixed-capacity POD mutation queue drained once at the top of the displayTask frame. Single writer, no model mutex.** Depth **64 records × 20B = 1.28KB**. Overflow policy and never-drop list in **D18**. (`MDL-03`) | Three producer contexts (esp-mqtt task, networkTask, sys_evt) against one consumer. A per-aggregate mutex must be held across the observer fan-out, and observers touch LVGL whose lock is taken on displayTask — a lock-order inversion that would also let a QSPI flush block the WiFi event task. The plan demands one choice in Phase 1 because "retrofitting it later means touching every model". |
| D18 | Queue overflow and drop policy | **Two record classes.** *Coalesce-latest* overwrites the pending slot for the same (aggregate, field) instead of taking a new one — RSSI/`signalStrength`, indoor/outdoor temp, `positionMs`, `volume`, `brightness`/`hue`, battery percent/voltage, clock epoch. *Never-drop* — `isPlaying`, `activeTarget`, `pendingRequest` (navigation intent), knob detent delta, `wifiConnected`/`mqttConnected`/`internetAvailable` transitions, `availability` transitions, the Climate pending publish. A never-drop record that cannot be enqueued retries 3× at 2ms, then logs `MUTQ overflow never-drop cls=<n>` at WARN and bumps a fault counter the P1 gate reads. An overflow counter is logged at WARN in all cases. (`MDL-03`, `MDL-40`) | Coalescing is what actually bounds the burst: a full-range knob sweep or an RSSI storm consumes one slot, not 100. Never-drop exists because a dropped state transition leaves the UI permanently wrong while a dropped RSSI sample costs nothing. The 64-record depth is in `MDL-01`'s RAM budget. Owner sign-off on the never-drop list is §9 Q38. |
| D2 | Publisher-side reads | **Seqlock double-buffered POD snapshot**, retry on odd sequence (`NET-05`, `MDL-41`) | Keeps the model single-writer while adapters on networkTask still read consistent state. No blocking, no allocation. |
| D3 | `Observable<T>` storage | **Fixed C array of `void(*)(const T&,void*)` + context; capacity templated per model; `subscribe()` returns false when full** (`MDL-04`) | `std::function` heap-allocates past the SBO size; notify runs every frame and Constraints forbid heap in the render path. `NoHeapScope` makes that an assertion (`MDL-06`). |
| D4 | Where the temperatures live | **`EnvironmentModel` keeps indoor/outdoor; `ClimateModel` owns only `targetTemp`, humidity, pressure, `indoorSampledAtMs`** (`MDL-18`, `UIX-21`) | INFO and CLIMATE render the same two numbers from the same topic and the same poller. Duplicating them recreates the one-name-several-values drift the refactor exists to remove. The plan deferred this to the CLIMATE design; the design decided it. |
| D5 | Knob authority | **Screen authoritative. Knob emits deltas; a coalesced `setpoint=N` becomes `N − lastN` clamped ±10, never a value. `onEnter()` pushes the mode's value down the write path.** (`MDL-28`, `UIX-11`, `PCKNOB-15`) | Plan 3.7. Sweep correction: `setpoint=N` is *dropped* today (`CommandHandler.cpp:137` splits on `:` only), so the cross-mode leak is latent — fixing the parser without the delta conversion would create it. |
| D6 | Knob wire format | **LF-terminate every knob→screen line; knob emits `+N`/`-N`; the screen runs an accumulating assembler that also accepts the legacy bare `+`/`-`.** Screen-side parser (`PCKNOB-11`) lands **before** the knob-side emitter (`PCKNOB-10`). | `Serial1.print` with no terminator makes two tokens arrive as `"++"`, matching nothing — every detent past the first per loop is lost today. Landing the emitter first would open a window in which all motion is dropped, which is the exact failure the change exists to fix; landing the tolerant parser first has no such window. |
| D7 | Inactivity ownership | **One clock in `ScreenRouter`, fed by `InputRouter`; `DisplayPowerController` acts; `SleepHandler` keeps only the CPU/light/deep ladder and is driven** (`DISP-15`, `INPT-28`) | Three clocks at 30s/30s/60s today, in lockstep only by accident of call order. |
| D8 | SLEEP | **A power state, not a screen. `AppScreen` has no SLEEP; the router holds the last real screen and wake repaints it.** (`DISP-08`, `DISP-14`, `INPT-29`) | SLEEP falls through the switch default today, renders nothing and pumps no LVGL. Wake must restore the user's screen, not force INFO. |
| D9 | Glyph coverage | **`0x20–0x7F` + `0xB0` (°) + `0x2022` (•) + `LV_SYMBOL_*`** — read from the generator line `-r 0x20-0x7F,0xB0,0x2022` in `lv_font_montserrat_20.c`. `U+2212`, en dash and em dash are absent: ASCII hyphen for signed deltas, `--` for placeholders, ASCII words for BELOW/ABOVE. (`UIX-01`) | The design's own feasibility note asserted ASCII-only in capitals and would have shipped `22.5C` on the flagship number of the flagship new screen. All three judges flagged it as blocking. |
| D10 | Type ramp | **Enable Montserrat 20/28/48; drop 14; `LV_FONT_DEFAULT` → 20.** Named roles only; views never name a face. Headroom is read from the `BLD-03` per-variant table, not from the deleted March build. (`SCRN-05`, `UIX-03`) | Zero first-party `text_font` calls, so nothing references 14 by name; keeping it as a fallback leaves the defect one forgotten label away. The default change grows unstyled labels ~43%, so Init and DeviceInfo are re-laid-out in the same change (`UIX-03`) and the four not-yet-split screens get an explicit interim 14px pin inside `SCRN-05`, each deleted by that screen's own split task. |
| D11 | Theme / refresh | **`THEME_DEFAULT_DARK=y`; unset `THEME_DEFAULT_GROW`; `TRANSITION_TIME=0`; drop `LV_USE_QRCODE`; keep `REFR_PERIOD=30`** (`UIX-02`) | The build is on the *light* theme, so any unstyled object paints a light rectangle on true black. GROW scales the one 290px-class tap target and invalidates a large area. 30ms bounds the worst case to one repaint per frame; halving it doubles draw cadence on a core already carrying WiFi plus two TLS sessions. All three judges rejected `REFR_PERIOD=16`. |
| D12 | Hit testing | **Coordinate maths in one shared helper: disc `r<132` (145 on Climate), rim annulus `140≤r≤190`, `132<r<140` and `r>190` dead.** `LV_OBJ_FLAG_CLICKABLE` is inert. (`UIX-06`) | No `lv_indev_drv_register` exists anywhere in `components/` or `main/` — LVGL does no hit-testing; touch is polled straight off `TouchPanel`. Five copies of this maths is how the tree got 21 polling sites. |
| D13 | Back gesture | **Hold-release gated on `getLastPressDurationMs() >= 3000`, implemented once in `InputRouter`, applied everywhere; shorter hold-releases consumed and discarded** (`INPT-08`, `SCRN-25`) | `ModeController` gates on 3000ms; `SoundController.cpp:117` and `LightController.cpp:229` exit on *any* hold-release. Back is the only navigation-out gesture on the new screens, and `MediaSeek` must discard rather than commit a pending scrub. |
| D14 | Prev/next | **swipe-left = `next()`, swipe-right = `previous()` on the media screens; the target overlay moves to an R3 rail tap; double-tap commits inside the overlay only. Over-scrub overrun kept as a bonus.** (`UIX-13`, `UIX-28`) | Requiring SEEK entry plus six overrun detents to skip a track is undiscoverable, and unreachable entirely when `durationMs == 0`. `EnvironmentInfoScreen.cpp:155` (swipe-right = back) is reconciled in the same pass. `UIX-28` publishes the resulting table and resolves double-tap against the design's immediate tap-to-play/pause. |
| D15 | New component layout | `model/`, `adapters/`, `input/`, `local_audio/`, `media_target/`, `spotify_client/`, `app_composition/`. **One** component for the bridge and all three pollers: `adapters/` (there is no `net_bridge/`). `ScreenRouter` lives in `controllers/`. | Keeps `model` a leaf, transport private, and gets `media` out of `controllers` REQUIRES so Phase 7 can close the cycle. Two names for one adapter layer left `net_bridge` with no registration task at all, which links late rather than failing at configure time. |
| D16 | Canonical PC tree | **`esp-idf/PC` canonical; delete `QNOB/PC`** (`PCKNOB-01`) | The stale fork has no `send_command` definition — every serial/TCP send raises `AttributeError` — and it sends commands the firmware never registers. |
| D17 | View lifecycle | **Decided by measurement, not by assertion.** `SCRN-28` instruments free heap and largest-free-block per navigation and runs the 50-cycle soak under both candidate policies; `SCRN-24` implements the winner. Default hypothesis: trees resident + `setVisible()`, with state-specific objects (PC ten-pip ring, seek ghost, overlay wedges, spinner) created on enter and destroyed on exit, and a recorded peak against the 64KB LVGL heap. | Build-on-enter fragments the single heap that also holds the DMA draw buffers. The decision must be made on largest-free-block after a soak, not on first-boot free bytes — so it cannot be an up-front decision with a 3h implementation task hanging off it. |
| D19 | LIGHT and the Instrument shell | **LIGHT is exempt from `UIX-04`/`UIX-05`.** `LightView` reproduces the `p0-fixed` layout on the new view/controller split; it does not adopt the ring shell. Recorded by `UIX-29`. | The winning design's `screens` array covers ModeWheel, MediaSpotify, MediaPC, MediaTargetSelect, Climate and MediaSeek — there is no Light screen, so "matches baseline" and "uses the ring shell" cannot both be acceptance criteria for `CTRL-21`. A LIGHT redesign is a new design task plus a rewrite of `CTRL-21`/`CTRL-22` (§9 Q39). |

### 2.1 Merge register (survivor ← folded)

Every folded id is a sweep id that has no row of its own; its work is inside the survivor named here.

| Work | Survivor | Folded |
|---|---|---|
| `InputEvent` | INPT-06 | DISP-02 |
| InputRouter + touch translation | INPT-07/08 | DISP-03 |
| Touch/gesture SFX out of DisplayController (+ inverted mapping fix) | INPT-13 | DISP-04, CTRL-12 |
| Knob read path / framing / consume-once | PCKNOB-09 | DISP-05, INPT-10 |
| Cut DisplayController over to the router | INPT-14 | DISP-06 |
| Splash timing machine → boot controller | SCRN-08 | **DISP-09** |
| Remove duplicate `+`/`-` consumption | INPT-21 | DISP-07(pt), CTRL-28, APP-08, MDL-35(pt), PCKNOB-13 |
| Kill the absolute-setpoint path / separator normalisation | INPT-22 | DISP-07(pt), CTRL-29, NET-14, PCKNOB-14 |
| Knob delta into InputEvent | INPT-12 | INPT-09, PCKNOB-12 |
| `ScreenController` + `NavigationRequest` | SCRN-04 | DISP-10 |
| First-paint brightness gate | SCRN-23 | DISP-16 |
| DisplayPowerController + two-timer collapse | DISP-14/15 | PERI-20, PERI-21 |
| Model structs | MDL-14…28 | NET-03, PERI-02 |
| Mutation queue / sync policy | MDL-03 | NET-04, APP-07 |
| C++ host harness and fakes | BLD-06 | MDL-05, SCRN-06 (NET-08's `FakeTransport` + poller suite survive as **NET-31**) |
| `hsvToRgb` → model | MDL-09 | CTRL-06 |
| Rate limit / debounce | MDL-12 | CTRL-09 |
| PC codec (device side) + PcMediaAdapter extraction | MEDIA-04/05/06 | CTRL-05, **CTRL-18**, NET-12, NET-13, PCKNOB-29, PCKNOB-30 |
| `IMediaTarget` | MEDIA-02 | CTRL-16 |
| MediaScreenController + binding machine | MEDIA-13/14 | CTRL-17, CTRL-19, CTRL-30, PCKNOB-31 |
| Delete SoundController | MEDIA-16 | CTRL-20 |
| MediaController → LocalAudio (+ move + register) | PERI-11 | CTRL-25, CTRL-26, APP-09, MDL-37, MEDIA-30, BLD-19 |
| LocalAudio dead-surface trim | PERI-12 | CTRL-27 |
| Light topics: declare / subscribe / bridge / scale | NET-15, NET-17, NET-18, NET-22 | CTRL-32, CTRL-33, CTRL-34, CTRL-35, MDL-38 |
| `LightController::setpoint` deletion + LedMode into the model | CTRL-13 / MDL-16 / CTRL-23 | **MDL-34** |
| Delete main.cpp MQTT lambda + screen callbacks | APP-15 | NET-21, CTRL-36 |
| Spotify NVS keys + rotation | APP-10/11 | MEDIA-18 |
| `configureSpotify` | APP-12 | MEDIA-19, PCKNOB-34 |
| PC seek + v2 reply | PCKNOB-22/23/24 | MEDIA-11, MEDIA-12 |
| Knob write path + liveness | PCKNOB-15/17 | INPT-24, INPT-26, MDL-36 |
| Route `esp32/sound/*` via the bridge | MEDIA-29 | PCKNOB-43 |
| KnobModel struct + policy + tests | MDL-28, MDL-29 | INPT-02, INPT-03, INPT-04 |
| PowerRules host tests | MDL-25 | **PERI-04** |
| Type ramp + font sdkconfig | SCRN-05 | BLD-14 |
| LVGL theme/feature config | UIX-02 | BLD-15 |
| Boot heap baseline | APP-02 | PERI-01, MEDIA-01 (`DISP-01` keeps draw-buffer geometry) |
| 3s back gate | SCRN-25 / INPT-08 | UIX-07 |
| Final per-component CMake tightening | BLD-28 | DISP-26, SCRN-27, CTRL-38, INPT-31, NET-25, APP-26, PERI-28, MDL-39, MEDIA-31, PCKNOB-44 |
| Cycle/layering proof | BLD-30 | DISP-27, PERI-29 |

`NET-20` is **not** folded: it survives as the transport decision (one MQTT client or two) and `APP-25` is its provisioning consequence.

Folded ids by prefix: DISP 11, CTRL 21, INPT 8, APP 4, MDL 7, NET 8, PERI 7, MEDIA 7, PCKNOB 9, SCRN 2, BLD 3 — **87 total**, plus the non-sweep `UIX-07`.

---

## 3. Phases

Dependencies are forward-only: every `Depends on` entry names a task in the same phase or an earlier one. `BLD-39` enforces this mechanically and `BLD-36` runs it.

### Phase 0 — Baseline (17 tasks, 29.5h)

Nothing in this phase changes what the device does after the capture. The crash/secret floor and the four repairs moved to Phase 1 WP1.0 precisely so `p0-baseline` stays a valid reference. One exception runs before the tag: `BLD-20` blanks the WiFi SSID/PSK in `sdkconfig.defaults.esp32c6`, because the repository is public and the tag would otherwise publish them; blanking a default that NVS overrides changes nothing on a provisioned device.

**WP0.1 Rotate, blank, then freeze the tree** (8 tasks, 10.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| PCKNOB-02b | Rotate all three leaked credentials **at their providers**: the HiveMQ broker account (`mqtt_handler.py:22`), the bench WiFi PSK (`sdkconfig.defaults.esp32c6:18`) and the OpenWeatherMap key (`WeatherHandler.h:28`); re-provision the bench; record whether git history is rewritten or the old values are accepted as burned | dev_notes/credential-rotation.md | 1 | — | Old broker user rejected by HiveMQ; old PSK rejected by the AP; old API key returns 401; device re-provisioned and reaching the broker before any capture starts |
| BLD-20 | Blank the WiFi SSID/PSK in `sdkconfig.defaults.esp32c6` (in the working tree, not yet on the remote — the repo is public and the `p0-baseline` tag would publish them); move bench locals to an untracked overlay | sdkconfig.defaults.esp32c6, build-variant.{sh,ps1} | 1 | PCKNOB-02b | `git grep` finds no SSID or PSK; a provisioned device still joins |
| BLD-37 | Decide `esp-idf/screen-test/`: delete, keep as a documented bring-up app, or convert to the components-only smoke target | screen-test/**, esp-idf/README.md | 1 | — | Decision recorded; the chosen outcome present in the tree; its `sdkconfig` named in `BLD-01` |
| BLD-01 | Untrack every generated `sdkconfig`/`.old` **by path, including `screen-test/`'s**, delete the stray `NUL`, extend `.gitignore` | .gitignore | 1 | BLD-37 | `git ls-files` lists no `sdkconfig`, `sdkconfig.old` or `NUL` at any path; all three variant builds still work |
| BLD-02 | Delete the stale in-tree `build/` (reports `qnub-screen`, 20 LovyanGFX references, dated Mar 2026) | build/ | .5 | BLD-37 | Fresh out-of-tree build reports `qnob-screen`; no `build/` tracked |
| BLD-04 | Settle `dependencies.lock` policy; delete the save/restore dance in the variant scripts | dependencies.lock, build-variant.{sh,ps1} | 2 | BLD-01 | Two consecutive builds leave `git status` clean; the lock's target matches the built target |
| BLD-39 | Dependency + roll-up lint over the WBS tables: parse (id, phase, hours, dependsOn), topological sort, assert `dep.phase <= task.phase`, flag unknown ids, recompute the §1 per-phase counts/hours and the §7 longest path | tools/wbs-lint.py, dev_notes | 2 | — | Exits 1 on a hand-injected backwards edge and on a hand-edited §1 total; exits 0 on this document and prints 318 ids / 952.0h |
| PCKNOB-01 | Retire `QNOB/PC`; declare `esp-idf/PC` canonical (**D16**) | PC/**, QNOB/PC | 2 | — | Canonical app launches, connects over serial, `help` round-trips; `QNOB/PC` gone |

**WP0.2 Measure, archive, tag `p0-baseline`** (9 tasks, 19.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| BLD-03 | Record `idf.py size` + `size-components` for all three variants with the `display`, `ui`, `lvgl`, `controllers` rows called out; **archive `build/project_description.json` per variant as `dev_notes/graph-baseline-<variant>.json`**; tag `p0-baseline` | build-all.sh, dev_notes | 3.5 | BLD-02, BLD-20 | `git grep -iE 'QNOB_WIFI_(SSID|PASSWORD)="[^"]'` returns nothing **before** tagging; per-variant table in dev_notes; three `graph-baseline-*.json` files each naming `qnob-screen`; commit tagged |
| APP-02 | Log free heap + DMA-internal free/largest at HAL init, after `LvglDisplay::init()`, after tasks start, with MQTT-TLS up, and on the SOUND screen | main.cpp | 1.5 | — | Five heap lines in boot/navigation order, transcribed into dev_notes |
| DISP-01 | Capture draw-buffer geometry verbatim (lines allocated, backend minimum 10 / preferred 30, even-align, rotation) | main.cpp, LvglDisplay.cpp | 1.5 | — | Init line transcribed verbatim into dev_notes |
| SCRN-01 | Per-screen baseline: 4 screenshots, one boot log, one full traversal log, measured first-INFO-paint duration | dev_notes | 2 | — | Baseline holds 4 `Changing mode` lines and one `ENV_ARC_V5` line; paint duration recorded in ms |
| INPT-01 | Input regression checklist observed on hardware: 8 gestures across 21 sites, 6 SFX, the fast-spin negative case | dev_notes | 3 | — | Every line ticked as observed, including `Unknown command: setpoint=` and the lost second detent |
| NET-01 | Capture the live MQTT/HTTP contract: topic, direction, payload grammar, `sender=` envelope, cross-checked against `MQTTManager.h` and the four literal-topic sites | dev_notes | 2 | — | Each row cites a broker capture or a `file:line` |
| MDL-01 | Set the model layer's static RAM budget — structs, every subscriber array, and **D18**'s 1.28KB queue (recommend 4KB all-in) | dev_notes | 2 | BLD-03 | Budget table plus the exact `size-components`/`.bss` command that measures it |
| BLD-38 | **The acceptance-threshold table**: max binary growth per phase in absolute bytes against `ota_0` = 3,276,800, max internal-DMA free-heap loss, max largest-free-block loss, required draw-buffer line count | dev_notes/acceptance-thresholds.md | 2 | BLD-03, APP-02, DISP-01 | Every "within baseline" / "binary shrinks" verification in §3–§4 resolves against a named row here; a deliberate 200KB growth is rejected by the table |
| BLD-45 | Report-only component-graph dump over `project_description.json`, called from both build scripts (prints the edge list, never fails) | check-component-graph.sh, build-variant.{sh,ps1} | 1.5 | BLD-03 | Every variant build prints its edge list; the live `display → controllers → display` cycle is visible in the output |

### Phase 1 — Floor, then `model` (79 tasks, 230.5h)

Two gates. **P1a** ends WP1.0 and tags `p0-fixed`, the reference every later phase diffs against. **P1b** ends WP1.1–WP1.4 with no visible regression against `p0-fixed` beyond the §8.2 allow-list.

**WP1.0 Floor: crashes, secrets, and the four never-worked paths** (15 tasks, 27.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| APP-01 | One numeric validator for every parse; `cmdKnobSetpoint`'s bare `std::stoi` aborts the chip remotely today | CommandHandler.{h,cpp} | 1.5 | — | `setpoint:abc` over serial **and** over MQTT logs a rejection, no reboot; `help` still works |
| APP-03 | `CommandSource` trust tags; refuse secret, destructive **and UI-freezing** commands from plaintext MQTT — the allowlist excludes `calibrateOrientation` and any `setMode` to a screen the switch does not handle | CommandHandler.{h,cpp}, main.cpp | 4.5 | APP-01 | `connectWifi` over plaintext refused with NVS unchanged; `calibrateOrientation` over plaintext refused **and the UI stays responsive** (freezes 30s today); the same commands over serial still execute |
| APP-04 | Bound the console accumulator at 512B (unbounded, ~6KB/s today) | CommandHandler.cpp | 1 | — | 2KB with no newline → one warning, flat heap, `help` still works |
| APP-05 | commandTask stack/priority into `hal_config.h`; stop prio-5 preempting the render loop on a single core | hal_config.h, main.cpp | 1.5 | — | Commands still land <100ms; stack high-water >1KB; no frame overrun logged during a command burst |
| APP-24 | Remove the hidden `loadAllConfigurations()` from `listNVSValues` | CommandHandler.cpp | .5 | — | No reload line in the output; uncommitted RAM values survive the call |
| PCKNOB-02 | Purge the committed HiveMQ credentials from HEAD; untrack `qnob_config.json` | PC/src/mqtt_handler.py, PC/src/config_handler.py | 2 | PCKNOB-01, PCKNOB-02b | `git grep -i hivemq` empty; a missing config file errors with a clear message instead of a traceback |
| PCKNOB-03 | Resolve the C6 knob UART conflict (`KNOB_TX=2`=`PWR_KEY`, `KNOB_RX=1`=`EXIO_INT`/`TOUCH_WAKEUP`) — reassign, or record the §9 Q1 answer and gate the feature | hal_config.h, SleepHandler.{h,cpp} | 3 | — | Knob pins unique in the C6 block; touch interrupt and PWR_KEY both still work; or Q1's answer recorded and `QNOB_ENABLE_KNOB_UART` defaulted off |
| CTRL-01 | Instrument the three silent failures: inbound media parse, light publish gate, double knob consumption | SoundController.cpp, LightController.cpp, DisplayController.cpp, CommandHandler.cpp | 2 | — | Log shows `no branch matched` on inbound state, `light gate closed`, and two consumers per detent |
| CTRL-02 | Strip the optional `sender=<id>,` prefix before matching; self-drop by sender | SoundController.cpp | 2 | CTRL-01 | Reaches `State received…`; a disc tap toggles PC playback (dead today) |
| CTRL-03 | Repair the LightController publish gate (`lightMQTTServerURL` has no writer, so LIGHT has never published) | LightController.cpp, main.cpp | 2.5 | CTRL-01 | `ledRing/#` receives colour, brightness and mode on a drag |
| CTRL-04 | Rate-limit the light publisher to ≤40 msg/s with a trailing flush (up to 300/s today) | LightController.{h,cpp} | 1.5 | CTRL-03 | Counted at the broker; the last message equals the value on screen |
| NET-02 | Log the raw dispatch payload and prefix presence at the MQTT callback | MQTTManager.cpp | 1 | NET-01 | `dispatch topic=… prefixed=1` proves the drop before `CTRL-02` fixes it |
| UIX-01 | Correct the glyph-coverage note and freeze the character rule (**D9**) | dev_notes/character-set.md | 1 | — | All temperature strings specified as `°C`; no `U+2212`, en dash or em dash anywhere in the specs |
| UIX-02 | Theme/feature config: DARK on, GROW off, TRANSITION_TIME 0, drop `LV_USE_QRCODE`, keep `REFR_PERIOD=30` (**D11**) | sdkconfig.defaults, sdkconfig.defaults.* | 1.5 | — | Three variants build; no light rectangle on any screen; no pressed-object scaling; QR objects gone; binary delta within the `BLD-38` row |
| BLD-40 | Re-capture `SCRN-01` and `INPT-01` on the repaired tree under the dark theme; record the four repaired behaviours as intended deltas; **tag `p0-fixed`** | dev_notes | 2 | APP-01, APP-03, APP-04, APP-05, APP-24, PCKNOB-02, PCKNOB-03, BLD-20, CTRL-01, CTRL-02, CTRL-03, CTRL-04, NET-02, UIX-01, UIX-02 | 4 new screenshots and a new traversal log under the dark theme; the checklist re-ticked with PC play/pause and LIGHT publish now passing; tag exists |

**WP1.1 Component, harness, the three irreversible decisions** (16 tasks, 50.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| BLD-05 | Dual-mode (IDF + host) `model` CMake over one shared `sources.cmake`; no project-local REQUIRES | model/CMakeLists.txt, model/sources.cmake | 2 | MDL-01 | `idf.py reconfigure` shows `model` with empty project-local reqs; the host project configures with `IDF_PATH` unset |
| BLD-24 | Register `components/adapters` (**D15**: REQUIRES model; PRIV_REQUIRES mqtt_manager wifi_manager internet_handler battery_handler esp_timer) — the single home for the bridge and all three pollers | adapters/CMakeLists.txt | 2 | BLD-05 | `reconfigure` shows public `reqs=model`; a file added under `adapters/` builds without a link error |
| BLD-41 | **Declare this phase's component edges**: `model` onto `controllers`, `display`, `ui`, `command_handler`, `storage` and `main`; `adapters` onto `main`. Additive only — Phase 7 tightens | 6 component CMakeLists.txt, main/CMakeLists.txt | 2 | BLD-05, BLD-24 | Every component that includes a `model/` header names it in REQUIRES; `fullclean` build on all three variants |
| BLD-06 | Host test project + single-header assert runner + `FakeClock` + `FakeTransport` + `FakeMediaTarget` | test/host/**, test/host/fakes/** | 5 | BLD-05 | `ctest` passes with `IDF_PATH` unset; all three fakes compile with no ESP header |
| BLD-07 | Match device flags (`-std=gnu++2b -fexceptions -fno-rtti`) + ASan/UBSan + `-Werror` | test/host/CMakeLists.txt | 2 | BLD-06 | A deliberate out-of-bounds subscriber read aborts under ASan |
| BLD-08 | Purity guard: fail on `esp_`/`freertos/`/`driver/`/`lvgl`/`nvs_`/`CONFIG_` anywhere under `model/` | test/host/check_model_purity.sh | 1.5 | BLD-06 | Exit 0 now; exit 1 after adding `#include "esp_log.h"` to one model header |
| BLD-09 | `run-host-tests.{sh,ps1}` (the PS1 delegates to WSL) | run-host-tests.{sh,ps1} | 2 | BLD-06, BLD-08 | Both print a ctest summary; the compiler used is `g++`, not the cross toolchain |
| BLD-10 | Glob-or-list suite registration convention covering every `test_*.cpp`, so a suite added by any later task runs with no CMake edit | test/host/CMakeLists.txt | 3 | BLD-06 | A new `test_zzz.cpp` is picked up and run with no CMake change; `ctest -N` lists every suite named in §8.1 |
| MDL-02 | `ModelTypes.h` (`FixedString<N>`, `Percent`, `Celsius`, `Millis`) + injectable `Clock.h` | model/include/** | 3 | MDL-01 | Purity guard green; no ESP or LVGL include anywhere under `model/` |
| MDL-03 | **D1 + D18** POD `ModelUpdate` (20B) + fixed 64-slot `MutationQueue` with per-record class, coalesce-in-place for coalesce-latest, 3×2ms retry then WARN + fault counter for never-drop, overflow counter; `drain(AppModel&, nowMs)`; host backing | model/MutationQueue.{h,cpp} | 8 | MDL-02 | FIFO order preserved; 100 volume records collapse to one slot; a never-drop record at capacity+1 logs `MUTQ overflow never-drop` and bumps the counter; `drain` asserts it runs on DisplayTask |
| MDL-04 | `Observable<T,Max>`: function-pointer + context array, notify-on-change only (**D3**) | model/Observable.h | 5 | MDL-02, MDL-03 | Over-capacity `subscribe()` returns false; setting an equal value notifies nobody; 0 allocations over 10k sets |
| MDL-06 | Test-only `operator new`/`delete` counter + `NoHeapScope` | test/host/support/HeapGuard.{h,cpp} | 4 | MDL-04, BLD-06 | Switching one model field to `std::string` fails the enclosing test |
| NET-05 | **D2** seqlock `ModelSnapshot` for cross-task reads | model/ModelSnapshot.{h,cpp} | 4 | MDL-03 | Reader retries on an odd sequence; no allocation; no blocking primitive in the type |
| MDL-40 | `test_mutation_queue.cpp`: FIFO, coalesce-in-place, capacity+1 for both classes, overflow counter, drain-task assertion | test/host/test_mutation_queue.cpp | 2 | MDL-03, BLD-06 | Five named cases green; the never-drop case fails if the retry loop is removed |
| MDL-41 | `test_model_snapshot.cpp`: two readers against one writer, 100k iterations, every snapshot from one generation | test/host/test_model_snapshot.cpp | 2 | NET-05, BLD-06 | Zero torn reads over 100k iterations; fails if the sequence increment is removed |
| MDL-30 | `AppModel` by value + `wireSubscriptions()` once at startup + `drainPendingMutations()` | model/AppModel.{h,cpp} | 3 | MDL-04, MDL-14, MDL-16, MDL-18, MDL-20, MDL-22, MDL-24, MDL-26, MDL-28, UIX-10 | `.bss` under the `MDL-01` budget; a subscribe after `wireSubscriptions()` returns false |

**WP1.2 Domain rules lifted out of views** (10 tasks, 33.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| MDL-07 | `Bands.h`: `TemperatureBand` (18/24/28 indoor, 0/15/28 outdoor), `BatteryBand` (20/40/60), `SignalBars` 0–4, **plus `bandProgress`** | model/Bands.{h,cpp} | 4 | MDL-02 | Sweep −20…50 hits the exact boundaries; band + progress reproduces the old lerp bit-for-bit |
| MDL-08 | Band tests including the `-1.0f` sentinel and NaN | test/host/test_bands.cpp | 3 | MDL-07, MDL-06 | `-1.0f` classifies as invalid, never COLD or CRITICAL; 0 allocations |
| MDL-09 | `hsvToRgb` + `channelFromRgb` → `model/ColorMath`, bit-exact | model/ColorMath.{h,cpp}, LightController.cpp | 3 | MDL-02 | A 1/360-step hue table is identical to today's output; both call sites' differing value args documented |
| MDL-10 | `formatDate`/`getMonthName` → `model/DateFormat` over `FixedString`, digit guard kept | model/DateFormat.{h,cpp} | 3 | MDL-02 | `2026/02/07` → `7 FEB`; `garbage` → `-- ---` |
| MDL-11 | ColorMath + DateFormat tests inside `NoHeapScope` | test/host/test_domain_helpers.cpp | 3 | MDL-09, MDL-10, MDL-06 | Malformed, empty, month 0 and month 13 all covered; 0 allocations |
| MDL-12 | `RateLimiter` (25ms media, 10ms light), `Debouncer` (400ms climate), `EchoSuppressor` — all take `nowMs` | model/RateLimit.{h,cpp} | 4 | MDL-02 | Denies at n−1ms, allows at n; `flush()` always emits the last value |
| MDL-13 | Rate-limit tests including a 100-change burst collapsing to one fire | test/host/test_ratelimit.cpp | 2 | MDL-12, MDL-06 | Burst collapse asserted; 0 allocations |
| PERI-03 | 13-point Li-ion SoC curve → `model/PowerRules`; `BatteryHandler` keeps acquisition only, and an all-12-samples-failed read stops returning the previous filtered voltage as fresh | model/PowerRules.{h,cpp}, BatteryHandler.{h,cpp} | 3 | MDL-02 | 13 knees + 3 midpoints match today's on-device percentages; a forced all-fail read reports invalid, not stale |
| APP-21 | Command parsers (`parseInt`, `connectWifi`, all five `configureMQTTServer` arities, static IP) → an ESP-free leaf | CommandParsers.{h,cpp} | 4 | APP-01, MDL-02 | One host case per arity, including the `password == "-"` reuse rule |
| PCKNOB-06 | Pure-python PC protocol module + pytest harness | PC/src/pc_media_protocol.py, PC/tests/** | 4 | PCKNOB-01 | `pytest` passes with pywin32 absent; `play` still works inside the app |

**WP1.3 The eight models** (17 tasks, 60.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| MDL-14 | `MediaModel`: activeTarget, volume, isPlaying, knobBinding, track title/artist/album, positionMs/durationMs, targetLabel, stateSynced, publish bookkeeping | model/MediaModel.{h,cpp} | 4 | MDL-04, MDL-12 | `sizeof` within the `MDL-01` budget; an equal set notifies nobody |
| UIX-10 | Amend Media/Climate for the design: **`availability[MediaTargetId]` — a two-slot array** (Spotify: OK/NO_TOKEN/NO_PREMIUM/OFFLINE/SYNCING; PC: OK/PC_OFFLINE/NO_PLAYER/NO_MQTT/STALE), `pendingSeekPercent`, `positionSampledAtMs`, `seekResolution`, `seekStepPercent`, `lastStateMs`, `track.id`, `indoorSampledAtMs`; plus `test_media_amendments.cpp` | model/MediaModel.h, model/ClimateModel.h, test/host/test_media_amendments.cpp | 5 | MDL-14, MDL-18, MDL-06 | Both slots settable independently (Spotify NO_TOKEN and PC PC_OFFLINE at once); `pendingSeekPercent` −1 distinguished from 0; `lastStateMs` staleness boundary at exactly 10s; `indoorSampledAtMs` 120s gate; 0 allocations |
| MDL-15 | MediaModel tests: clamp, out-of-range rejection, echo suppression, truncation, **the two `availability` slots independent** | test/host/test_media_model.cpp | 3.5 | MDL-14, UIX-10, MDL-06 | Empty-payload rejection pinned (silently mutes today); 0 allocations |
| MDL-16 | `LightModel`: brightness 0–100 only, hue, saturation, `LedMode`, publish bookkeeping; `setpoint` deleted | model/LightModel.{h,cpp} | 3 | MDL-04, MDL-09, MDL-12 | 0–255 inbound handled per the single wire-scale rule; hue clamps at the `i%6` boundary |
| MDL-17 | LightModel tests: clamps, `LedMode` cycle, one publish per window, inside `NoHeapScope` | test/host/test_light_model.cpp | 3.5 | MDL-16, MDL-06 | First-ever execution of the `LedMode` transition; 0 allocations |
| MDL-18 | `ClimateModel` (**D4**): targetTemp, humidity/pressure + validity, `indoorSampledAtMs`, `targetBand` | model/ClimateModel.{h,cpp} | 2 | MDL-04, MDL-07 | `indoorValid` false until the first write; a sane default target inside 16–30 |
| MDL-19 | ClimateModel tests: validity gate, NaN/∞ rejection, clamp 16–30, band tracking, inside `NoHeapScope` | test/host/test_climate_model.cpp | 2.5 | MDL-18, MDL-06 | A malformed MQTT float is rejected and never stored; 0 allocations |
| MDL-20 | `EnvironmentModel`: fixed-char date/time/day/formatted, both temps + validity bits, bands + progress, 0.5°C hysteresis | model/EnvironmentModel.{h,cpp} | 4 | MDL-04, MDL-07, MDL-10 | 0.4°C suppressed, 0.6°C admitted; a 40-char date truncates instead of overflowing |
| MDL-21 | EnvironmentModel tests including the sentinel and band continuity | test/host/test_environment_model.cpp | 3 | MDL-20, MDL-06 | 0 allocations over 10k updates; a true 18.0°C is no longer discarded as the sentinel |
| MDL-22 | `ConnectivityModel`: wifi, internet, mqtt, strength 0–4, `SignalBars`, `mqttLightConfigured` | model/ConnectivityModel.{h,cpp} | 3 | MDL-04, MDL-07 | Strength >4 rejected; five distinct inputs give five distinct bar values (top bar unreachable today) |
| MDL-23 | ConnectivityModel tests: the four flags independent, inside `NoHeapScope` | test/host/test_connectivity_model.cpp | 2.5 | MDL-22, MDL-06 | `mqtt=false` does not clear `wifi`; 0 allocations |
| MDL-24 | `PowerModel`: percent, voltage, connected, presenceKnown, percentValid, charging, currentMa, band | model/PowerModel.{h,cpp} | 3 | MDL-04, MDL-07 | `-1.0f` sets `percentValid` false and yields no band |
| MDL-25 | PowerModel + `PowerRules` tests: 13 knees + 3 midpoints, clamps above 4.20V and below 3.30V, sentinels, presence ≠ connected, inside `NoHeapScope` | test/host/test_power_rules.cpp | 2.5 | MDL-24, PERI-03, MDL-06 | Known-absent cell = `presenceKnown` true, `connected` false; 0 allocations |
| MDL-26 | `NavigationModel` + **`AppScreen`** (no SLEEP, **D8**) + **`KnobMode{LIGHT,CLIMATE,MEDIA}`**; one `lastActivityMs`; `pendingRequest` as a single-consumption intent; **`initProgressPct`** | model/NavigationModel.{h,cpp}, model/AppScreen.h | 5 | MDL-04 | Every screen has a transition entry; a request is consumed exactly once; `initProgressPct` is the only home for boot progress |
| MDL-27 | NavigationModel tests: single consumption, one activity clock, no transition to a dropped screen, inside `NoHeapScope` | test/host/test_navigation_model.cpp | 3.5 | MDL-26, MDL-06 | A flag-based implementation fails `request_consumed_once`; 0 allocations |
| MDL-28 | `KnobModel` (**D5**): detentCount, link liveness, haptic config (120 / 1.0 / 0.55 / 3.0), pendingWrite, `applyDelta`, `burstToDelta` | model/KnobModel.{h,cpp} | 6 | MDL-04, MDL-12 | Defaults equal the knob firmware's; from 40, `burstToDelta(43)` = +3; the ±10 clamp asserted |
| MDL-29 | KnobModel reconciliation tests — the most valuable suite in the tree — inside `NoHeapScope` | test/host/test_knob_model.cpp | 4.5 | MDL-28, MDL-06 | `mode_switch_does_not_leak_setpoint` fails against today's dispatch; net-zero deltas, absolute-never-stored, clamp at both ends, link timeout; 0 allocations |

**WP1.4 Shadow population and ownership fixes** (21 tasks, 59.0h)

Every task here that writes a model depends on `MDL-03`, so no writer can satisfy its own verification while bypassing the queue.

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| APP-06 | Construct `AppModel` before any consumer; add `model` to main's REQUIRES | main.cpp, main/CMakeLists.txt | 2 | MDL-30, BLD-41 | One `MODEL:` line per update, tracking what the screens display |
| MDL-31 | Shadow-write every existing callback **by posting mutation records**, never by direct assignment | main.cpp | 6 | MDL-30, MDL-03, APP-06 | Behaviour identical to `p0-fixed`; `grep` finds no direct model setter call outside `drain()` |
| MDL-32 | Compile-gated `ModelTrace`, no allocation, off by default, **plus the off-displayTask write assertion**: log the task name at every drain and count any model write observed from another task | model/ModelTrace.{h,cpp} | 4.5 | MDL-31 | A knob + INFO walk logs values matching the panel; the off-task counter reads exactly 0 at the P1b gate and non-zero if one writer is reverted to a direct set |
| INPT-05 | Shadow-populate `KnobModel` from the existing knob path, via the queue | DisplayController.cpp, display/CMakeLists.txt | 2 | MDL-28, MDL-03, BLD-41 | `detentCount` tracks SOUND volume 1:1 over 20 detents |
| CTRL-07 | Shadow-populate `MediaModel` from SoundController, via the queue | SoundController.{h,cpp}, controllers/CMakeLists.txt | 2.5 | MDL-14, MDL-03, CTRL-02, BLD-41 | Logged volume and isPlaying match the arc and the state label; the MQTT-task write goes through the queue |
| CTRL-08 | Shadow-populate `LightModel` (field named `brightness`, not `waterLevel`), via the queue | LightController.{h,cpp}, controllers/CMakeLists.txt | 2 | MDL-16, MDL-03, BLD-41 | Logged values track the fill and the hue during a drag |
| NET-09 | Collapse the two `internetAvailable` owners; MQTT takes an injected predicate; delete the unreachable "reconnected → re-check" branch | WiFiManager.{h,cpp}, MQTTManager.{h,cpp}, InternetHandler.{h,cpp} | 3 | NET-01 | MQTT connects <10s after IP; a WAN-only outage is agreed by both consumers within one cycle |
| NET-10 | Remove cross-task `std::string` mutation from the TimeHandler/WiFiManager getters | TimeHandler.{h,cpp}, WiFiManager.{h,cpp} | 3 | MDL-02 | No string member written outside its owning task; a 30-minute poisoned-heap soak is clean |
| NET-06 | `ConnectivityPoller` on networkTask, push-on-change through the queue; RSSI→`SignalBars` leaves the driver | adapters/ConnectivityPoller.{h,cpp} | 4 | MDL-03, NET-09, MDL-22, BLD-24 | Exactly one update per change and none while steady; bars 0–4 all reachable |
| NET-07 | `EnvironmentPoller` posts epoch + outdoor temp (never formatted strings), minute-granularity filter; **timezone and NTP servers move from the literal `3`/`pool.ntp.org` into Kconfig/NVS with the DST decision recorded** | adapters/EnvironmentPoller.{h,cpp}, Kconfig.projbuild, TimeHandler.cpp | 4.5 | MDL-03, NET-10, MDL-20, BLD-24 | ≤1 update/min; the colon still blinks at 1Hz; the offset comes from config and the DST answer is in dev_notes (§9 Q28) |
| NET-31 | `test_pollers.cpp` over `FakeTransport`: push-on-change vs steady, RSSI→bars, minute-granularity filtering, battery sentinel pass-through | test/host/test_pollers.cpp | 4 | NET-06, NET-07, PERI-06, BLD-06 | Green with no `esp_wifi` header in the translation unit; a steady RSSI produces zero updates |
| PERI-05 | Real `isPresenceKnown()` (AXP2101 only); stop using "telemetry present" as "battery present" | BatteryHandler.{h,cpp}, main.cpp | 2 | — | C6 reports an estimate and says so; `[AXP2101]` flips on cell removal |
| PERI-06 | `BatteryPoller` pushes into `PowerModel` through the queue | adapters/BatteryPoller.{h,cpp} | 3 | MDL-24, MDL-03, PERI-05, BLD-24 | The logged model tracks the DeviceInfo label |
| PERI-07 | Move battery acquisition off the 16ms render loop to networkTask | main.cpp | 1 | PERI-06 | Sampling attributed to networkTask in the trace; the reading still refreshes with WiFi down |
| PERI-08 | Delete never-populated telemetry (`currentMilliamps` = −1, `lastRawSpread`, `isCharging()`) | BatteryHandler.{h,cpp} | 1 | PERI-05 | Build clean; binary delta within the `BLD-38` row; percentage unchanged |
| NET-11 | Point the four screen callbacks at the model instead of the drivers (shim; screens still read their own fields) | main.cpp | 2 | NET-06, NET-07, PERI-06, MDL-03 | Same visible update rates as `p0-fixed`; each logged driver/model pair agrees |
| NET-28 | Weather API key out of the header into Kconfig/NVS; HTTPS via the cert bundle | WeatherHandler.{h,cpp}, Kconfig.projbuild | 3 | PCKNOB-02b | 200 over `https://`; an empty key logs invalid config and performs no fetch |
| NET-29 | Make the 2KB HTTP response buffer instance-owned; handle chunked responses | WeatherHandler.{h,cpp} | 2 | — | Two back-to-back fetches both parse; a truncated body logs a failure instead of parsing garbage |
| NET-30 | Time-box the blocking calls (3×15s WiFi, 10s NTP, 10s HTTP) so the pollers keep cadence | WiFiManager.cpp, TimeHandler.cpp, main.cpp | 5 | NET-06, NET-09 | No networkTask iteration >200ms; MQTT reconnects <10s after an AP power cycle |
| PCKNOB-04 | Write the knob link protocol v2 contract: framing, tokens, authority, `=` vs `:` (**D6**) | knob_controller/knob_link_protocol.md | 2 | — | Every token has exactly one producer and one consumer, each cited to a `file:line` |
| PCKNOB-05 | Delete the knob's unread `last_setPoint` | Motor Driver/HapticController3/HapticController3.ino, FOC.ino | .5 | — | Sketch compiles; `status` output still live |

### Phase 2 — Unify input (33 tasks, 103.0h)

`PERI-11` moves into this phase so the `INPT-13 ↔ PERI-11` cycle in the previous draft is broken by ordering rather than by a private edge: `PERI-11 → PERI-10 → INPT-13`. `INPT-21`/`INPT-22` also land here so the P2 gate "one detent = exactly one step" is deliverable by this phase's own task list.

**WP2.0 Component edges and the LocalAudio move** (5 tasks, 11.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| INPT-06 | `InputEvent{type,x,y,gesture,durationMs,knobDelta}`; note in the header that the knob has no push button | input/InputEvent.h | 2 | — | Compiles in the IDF and host builds; `sizeof` <16B |
| BLD-11 | Register `components/input` (REQUIRES model; PRIV_REQUIRES touch_panel knob_controller esp_timer) | input/CMakeLists.txt, main/CMakeLists.txt | 1.5 | INPT-06, BLD-41 | Public reqs exactly `model`; one tap produces exactly one event on device |
| PERI-11 | Rename `MediaController`→`LocalAudio`, move to `components/local_audio`, **register it**, update all eight call sites atomically | local_audio/**, local_audio/CMakeLists.txt, media/**, 4 component CMakeLists.txt, main.cpp | 3.5 | BLD-41 | `grep MediaController` clean; `local_audio` appears in the `BLD-45` edge dump; tap/swipe SFX and the three recording commands still work |
| BLD-42 | **Declare this phase's component edges**: `input` onto `controllers`, `ui`, `display` and `main`; `local_audio` replaces every `media` edge; `display` drops its `controllers` include path where `PERI-09` removed the include | 5 component CMakeLists.txt, main/CMakeLists.txt | 2 | BLD-11, PERI-11 | `fullclean` build on all three variants; the `BLD-45` dump shows no component including a header it does not require |
| PERI-12 | Delete the dead media API (`playReactionSound`, `recordToFile`, gain setters + the dead gain loop, `kStartupSound` 317 lines, three unreachable effect fallbacks, `SDCard::{readFile,writeFile,writeTextFile}`, `logDirectoryTree`) and trim LocalAudio's public surface | media/**, local_audio/** | 2.5 | PERI-11 | Binary delta within the `BLD-38` row; every one of the six effects sounds identical |

**WP2.1 InputRouter** (11 tasks, 41.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| INPT-07 | `InputRouter` with a fixed 8-entry event ring + overflow counter — one poll can latch release, press and a knob delta together | input/InputRouter.{h,cpp} | 6 | INPT-06, BLD-11 | Ring high-water stays below capacity and overflow reads 0 under a 60s stress walk |
| INPT-08 | Translate the seven latched `TouchPanel` flags into events; synthesise `TOUCH_MOVE` every poll while pressed; **implement D13's 3000ms back gate here once**, consuming and discarding shorter hold-releases; software double-tap counter (1000ms) + 250ms stale-release guard | input/InputRouter.{h,cpp} | 6 | INPT-07 | One event per `p0-fixed` checklist gesture with the same `durationMs`; a 1s hold-release produces no back event, a 3.5s one does |
| INPT-11 | Activity tracking + `consumeWakeEvent()` dropping the wake press **and** its release | input/InputRouter.{h,cpp} | 4 | INPT-08 | A wake tap does not navigate on any screen |
| INPT-12 | Dual-path wiring in `main.cpp` (poll before update), log every event, route the knob delta | main.cpp | 3 | INPT-08, INPT-11 | The whole `p0-fixed` checklist passes through the old path, with exactly one router line per gesture |
| UIX-06 | **D12** one shared hit-test helper: disc `r<132` (145 on Climate), rim annulus `140≤r≤190`, dead bands elsewhere | input/InputRouter.{h,cpp}, input/HitZones.h | 3 | INPT-08 | `grep LV_OBJ_FLAG_CLICKABLE` finds none on any arc; a rim tap and a disc tap dispatch distinctly in the log |
| PERI-10 | Touch-feedback interface on the LocalAudio side so `input` never requires `controllers` | local_audio/LocalAudio.h, local_audio/ITouchFeedback.h | 3 | INPT-06, PERI-11, PERI-09 | All six effects fire; the `BLD-45` dump shows no new edge out of `input` |
| INPT-13 | SFX triggers → `InputSfxListener` behind that interface, **fixing the inverted press/release mapping** | input/InputSfxListener.{h,cpp}, DisplayController.cpp | 3 | INPT-12, PERI-10 | Click at finger-down and release at finger-up (reversed today, nothing on the real press); all four swipes fire |
| INPT-14 | Delete DisplayController's input block and its touch/knob members | DisplayController.{h,cpp}, main.cpp | 4 | INPT-12, INPT-13 | Checklist re-walked; the knob `Unknown command` spam stops |
| SCRN-25 | Normalise gesture semantics once (tap vs swipe, entry stale window, **D13** wiring) and fix every hint string on the five live screen classes | SoundController.cpp, LightController.cpp, DeviceInfoScreen.cpp, ModeController.cpp, EnvironmentInfoScreen.cpp, dev_notes/gesture_table.md | 3 | INPT-08 | The gesture table matches every on-screen hint and the one-event log; DeviceInfo's hint no longer contradicts its behaviour |
| PERI-09 | Activity to `SleepHandler` from `InputRouter`; drop the `sleep_handler` include from `display` | SleepHandler.h, DisplayController.{h,cpp} | 3 | INPT-06, BLD-11 | Stage 0 at 20s; a touch **and** a lone detent each exit power save |
| MDL-33 | `KnobModel`↔`InputRouter` boundary: `applyDelta`, `noteLinkActivity`, one sign convention | model/KnobModel.{h,cpp}, input/InputRouter.cpp | 3 | MDL-28, INPT-06 | `grep detentCount` shows writes only inside `KnobModel` |

**WP2.2 Knob wire format — parser first (D6)** (5 tasks, 11.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| PCKNOB-07 | Gate the per-loop `Serial.println(shaftAngle)` behind `HAPTIC_DEBUG` | Motor Driver/HapticController3/FOC.ino | 1 | — | Console quiet; measured FOC loop rate rises |
| PCKNOB-09 | Accumulating line assembler; fix the 512B-UART-ring-read-into-a-256B-stack-array overflow and the cross-task `std::string` copy | KnobController.{h,cpp} | 4 | PCKNOB-04 | 50 detents inside 1s: one line each, no panic, no truncation |
| PCKNOB-11 | `consumeDelta()` parses to a signed clamped int and clears on read, **accepting both the legacy bare `+`/`-` and the new `+N`/`-N`** | KnobController.{h,cpp} | 3 | PCKNOB-09 | `+3,-1,+,+abc,+99999` → 3, −1, 1, 0, clamped; no throw; a knob still on old firmware keeps working |
| PCKNOB-08 | LF-terminate knob writes; remove the per-character `delay(3)` from inside the FOC loop | FOC.ino, serial.ino | 1.5 | PCKNOB-04, PCKNOB-11 | Every token ends `0x0A`; no motor jolt during an inbound write |
| PCKNOB-10 | Knob emits `+N`/`-N` and drains its pending counter instead of clamping 0–100 | FOC.ino | 2 | PCKNOB-08, PCKNOB-11 | `Sent to display:+7`; the N values sum to the detents turned over a 5-revolution sweep |

**WP2.3 Cut the 21 polling sites over** (8 tasks, 28.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| INPT-15 | DeviceInfoScreen — 3 sites → `onInput` | DeviceInfoScreen.{h,cpp}, main.cpp, ui/CMakeLists.txt | 3 | INPT-14, BLD-42 | Swipe-down and tap both return to INFO, redrawn |
| INPT-16 | EnvironmentInfoScreen — 3 sites; delete the `forceFullRefresh` guard | EnvironmentInfoScreen.{h,cpp}, main.cpp | 3 | INPT-14, INPT-11, BLD-42 | Three navigations plus wake-stays-put |
| INPT-17 | ModeController — 8 sites; delete `ignoreNextRelease` and the local stale guard | ModeController.{h,cpp}, main.cpp | 6 | INPT-14, INPT-11, INPT-08, BLD-42 | Tap does nothing, double-tap selects, a 3.5s hold backs out, the entry release does not self-select |
| INPT-18 | SoundController — 2 sites; knob arrives as `KNOB_DELTA` | SoundController.{h,cpp}, main.cpp, controllers/CMakeLists.txt | 3 | INPT-14, BLD-42 | Tap toggles only after sync; one detent = one step |
| INPT-19 | LightController — 5 sites including the continuous drag | LightController.{h,cpp}, main.cpp, controllers/CMakeLists.txt | 4 | INPT-14, INPT-08, BLD-42 | Drag smooth on both axes; hold-release exits only past 3s |
| INPT-20 | Prove the cutover: remove every `TouchPanel*` member, parameter and include from the five screen classes | DeviceInfoScreen.h, EnvironmentInfoScreen.h, ModeController.h, SoundController.h, LightController.h | 2 | INPT-15, INPT-16, INPT-17, INPT-18, INPT-19 | `grep touchPanel->` hits only `input/` and `main.cpp` |
| CTRL-10 | SoundController tap/hold entirely via `onInput`, then delete the member | SoundController.{h,cpp}, main.cpp | 3 | INPT-18, CTRL-02 | Neither gesture fires twice |
| CTRL-11 | LightController drag entirely via `onInput`; thresholds stay in the controller | LightController.{h,cpp}, main.cpp | 4 | INPT-19, CTRL-04 | No stair-stepping on a slow drag; hold-release exits |

**WP2.4 Knob ownership, part 1 (plan 3.7)** (4 tasks, 11.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| INPT-21 | Remove CommandHandler's knob drain, `commands["+"]`/`["-"]`, both handlers and `registerKnobController` | CommandHandler.{h,cpp}, main.cpp | 3 | INPT-14, MDL-28, PCKNOB-11 | One detent = exactly one step (1–3 today); no `Setpoint incremented.` line remains |
| INPT-22 | Retire the absolute-setpoint write: guard the parse, convert to a delta per **D5** or reject; delete DisplayController's three setpoint proxies | CommandHandler.{h,cpp}, DisplayController.{h,cpp} | 3 | INPT-21, MDL-28 | `setpoint` and `setpoint:abc` warn and keep running (both reboot today); a fast spin in SOUND then a switch to LIGHT leaves brightness untouched |
| INPT-23 | Free the knob UART pins on C6, or gate PWR_KEY/wake so they never own them | hal_config.h, main.cpp, SleepHandler.{h,cpp} | 4 | PCKNOB-03 | The knob answers a scoped TX query with no reboot; PWR_KEY and touch wake both still work |
| INPT-27 | Delete the unused `knob_controller_*` C wrapper and its file-static string | KnobController.{h,cpp} | 1 | PCKNOB-09 | `grep knob_controller_` hits only CMake targets |

### Phase 3 — Split screens (117 tasks, 383.5h; §4 carries 33 of them)

Screen deletions that need the router (`SCRN-16`, `SCRN-23`) are in Phase 4. Deletions that only need `DisplayController` rewired to hold the new controller (`MEDIA-16`, `CTRL-24`, `SCRN-21`, `SCRN-09`, `SCRN-12`) stay here and **keep the existing MQTT publish/subscribe path** until Phase 5 removes it, so no Phase-3 task depends on Phase 5.

**WP3.0 Contracts and this phase's component edges** (3 tasks, 6.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| BLD-43 | **Declare this phase's component edges**: `ui` gains `model` + `input`; `controllers` gains `model` + `input` + `media_target`; add `ui/theme`, `ui/views`, `controllers/screens` to SRCS and INCLUDE_DIRS | ui/CMakeLists.txt, controllers/CMakeLists.txt, main/CMakeLists.txt | 2 | BLD-42 | A file under `ui/theme/` and one under `controllers/screens/` both compile and link; `fullclean` build on three variants |
| SCRN-03 | `IScreenView` with `build()`, `destroy()`, `root()`, `setVisible()`, typed `render`; the LVGL lock is a documented caller precondition plus a debug assert | ui/IScreenView.h, LvglDisplay.{h,cpp} | 2 | BLD-43 | `build()`×2 then `destroy()`×2 leaves no assert and flat heap |
| SCRN-04 | `ScreenController` base (`onEnter`, `onExit`, `onInput`, `tick`) + `NavigationRequest` as a polled destructive read | controllers/ScreenController.h, controllers/NavigationRequest.h | 2.5 | MDL-26, INPT-06, BLD-43 | A stub returns a request once and then reports invalid |

**WP3.1 InitializationScreen** (3 tasks, 10.5h)

Model bindings: `NavigationModel::{initProgressPct, activeScreen}`; `ConnectivityModel::{wifiConnected, mqttConnected}` for the status line.

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| SCRN-07 | `InitializationView` with `render(const NavigationModel&)` reading `initProgressPct` (arc rotation 120 / bg 0–300 / range 0–100, logo, percent, status) | ui/views/InitializationView.{h,cpp} | 3 | SCRN-03, SCRN-05, MDL-26 | Arc fills from the `p0-fixed` start angle; progress comes from the model, not a view member |
| SCRN-08 | `BootScreenController` (**folds DISP-09**): one ramp, one start stamp, hold→INFO, drop the dead `setWiFiStatus`/`setMQTTStatus`, and lift the splash timing machine out of `DisplayController::update()` | controllers/screens/BootScreenController.{h,cpp}, DisplayController.{h,cpp} | 5 | SCRN-04, SCRN-07, INPT-14 | Host: 0→100 over 2500ms and a NavigationRequest at 2900ms with no backward jump. Device: progress monotonic once, INFO ~400ms later; the boot status line is no longer stuck on `Starting \| MQTT pending` |
| SCRN-09 | Retire `InitializationScreen`; delete `startupSplashTask` and the 30ms pseudo-join's screen ownership | InitializationScreen.{h,cpp}, main.cpp | 2.5 | SCRN-08 | `grep InitializationScreen` empty; the arc animates through SD and WiFi init |

**WP3.2 DeviceInfoScreen** (3 tasks, 8.5h)

Model bindings: `PowerModel::{percent, percentValid, presenceKnown, connected, band}`; `ConnectivityModel::{wifiConnected, mqttConnected, bars}`; `NavigationModel::activeScreen`.

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| SCRN-10 | `DeviceInfoView`: 5 rows, 13 handles, band→colour only, injected width/height so `scalePx` never dereferences a null display, bars mapped 0–4 | ui/views/DeviceInfoView.{h,cpp} | 4 | SCRN-03, SCRN-05, MDL-22, MDL-24 | All five battery states reachable and matching `p0-fixed`; the top signal bar now reachable; `lvglReady` no longer latched before `buildUi` |
| SCRN-11 | `DeviceInfoScreenController`; drop `lastActivityTime` and `resetScreen` | controllers/screens/DeviceInfoScreenController.{h,cpp} | 3 | SCRN-04, SCRN-10, NET-06 | One transition per swipe; pulling the AP flips the row in <2s with no touch |
| SCRN-12 | Retire `DeviceInfoScreen`; relocate or delete `DeviceBatteryStatus` | DeviceInfoScreen.{h,cpp}, DisplayController.{h,cpp}, main.cpp | 1.5 | SCRN-11 | `grep DeviceInfoScreen` empty; INFO↔DEVICE_INFO round-trips |

**WP3.3 EnvironmentInfoScreen (911 lines) and the view lifecycle** (9 tasks, 31.5h)

Model bindings: `EnvironmentModel::{formattedDate, time, dayOfWeek, indoorTemp, outdoorTemp, validity bits, bands, bandProgress}`; `NavigationModel::activeScreen`.

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| SCRN-02 | **Hotfix, ships alone:** leave-animation freeze — distinct shrink callbacks, `deleted_cb` decrement, skip entry animations while navigating, wall-clock backstop | EnvironmentInfoScreen.{h,cpp} | 3 | — | Publish indoor every 200ms and swipe right ×20: reaches the wheel every time (hangs today) |
| SCRN-17 | `EnvironmentView` part 1: full geometry chain, 4 outdoor segments with colours and angles, indoor arc, gradient disc, 5 labels | ui/views/EnvironmentView.{h,cpp} | 5 | SCRN-03, SCRN-05, UIX-04 | Screenshot compare against `p0-fixed` at indoor 22 / outdoor 12 |
| SCRN-18 | Part 2: `render(const EnvironmentModel&)` from pre-classified bands; label positioning becomes change-driven | ui/views/EnvironmentView.{h,cpp} | 4 | SCRN-17, MDL-20 | Label re-centres at every digit/sign change and only then; an idle INFO screen shows no flushes |
| SCRN-19 | Four distinct animation identities, `user_data` always set, `destroy()` cancels all four | ui/views/EnvironmentView.{h,cpp} | 4 | SCRN-17, SCRN-02 | 20 leaves under MQTT load all complete; 0 animations remain after teardown |
| SCRN-20 | `EnvironmentScreenController`: `onInput`, leave handshake as a callback, 1Hz blink in `tick`, drop the per-frame date/time pull and three dead members | controllers/screens/EnvironmentScreenController.{h,cpp} | 4 | SCRN-04, SCRN-19, NET-07 | All four navigation edges plus a correct repaint after 30s idle |
| SCRN-21 | Retire `EnvironmentInfoScreen`, deleting the 64 unreachable `if (false && …)` lines and the write-only members; the new controller keeps the existing indoor-temp callback until Phase 5 | EnvironmentInfoScreen.{h,cpp}, DisplayController.{h,cpp}, main.cpp | 2.5 | SCRN-20 | `grep EnvironmentInfoScreen` empty; publishing 24.5 moves the readout in <1s |
| SCRN-22 | Remove `std::string` and per-frame heap traffic from the render path | ui/views/EnvironmentView.cpp, ui/views/DeviceInfoView.cpp, controllers/screens/EnvironmentScreenController.cpp | 3 | SCRN-18, SCRN-20, MDL-20 | ~6 allocations/frame → 0; flat heap over 10 minutes; rendered strings byte-identical to `p0-fixed` |
| SCRN-28 | **Lifecycle measurement (decides D17):** instrument free heap and largest-free-block per navigation, run the 50-cycle soak under both candidate policies, record the peak object count against the 64KB LVGL heap | ui/**, dev_notes/lvgl-lifecycle.md | 3 | SCRN-19, SCRN-10 | Two soak tables recorded; the chosen policy named with its largest-free-block figure, checked against the `BLD-38` row |
| SCRN-24 | Implement `destroy()` across all four views under the policy `SCRN-28` chose | ui/views/*.cpp, ui/IScreenView.h | 3 | SCRN-28, SCRN-07, SCRN-13 | 50 navigation cycles: free heap **and** largest free block flat; no double delete under ASan-equivalent device asserts |

**WP3.4 Media adapters and controller, device side** (12 tasks, 40.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| MEDIA-02 | `IMediaTarget` + `MediaTargetId`, `MediaCaps`, `MediaError` | media_target/IMediaTarget.h | 3 | MDL-14 | A stub target compiles on the host with no ESP include |
| BLD-12 | Register `components/media_target` (REQUIRES model; PRIV_REQUIRES mqtt_manager esp_timer) | media_target/CMakeLists.txt, controllers/CMakeLists.txt | 1.5 | MEDIA-02, BLD-43 | Public reqs are `model` only; a volume round trip works on device |
| MEDIA-03 | `MediaTargetRegistry` (fixed 2 slots) + static `NullMediaTarget` | media_target/MediaTargetRegistry.{h,cpp}, media_target/NullMediaTarget.h | 3 | MEDIA-02, BLD-12 | An unregistered target returns `NotAvailable`, no crash, no `operator new` |
| MEDIA-04 | `PcMediaAdapter` skeleton + pure v1 encoders in `PcProtocol` (**folds CTRL-18**, the SoundController MQTT extraction) | media_target/PcMediaAdapter.{h,cpp}, media_target/PcProtocol.{h,cpp} | 4 | MEDIA-02, BLD-12 | Encoder output byte-identical to the three literals SoundController publishes today |
| MEDIA-05 | `parsePcResponse`: strip `sender=`, report the sender, accept v1+v2 fields in any order, reject out-of-range, replace the magic offsets | media_target/PcProtocol.{h,cpp} | 5 | MEDIA-04 | The `NET-01` captured payloads all parse; a comma-in-title survives; garbage never sets volume; an empty payload no longer mutes |
| MEDIA-06 | Fixed SPSC ring of `char[]` frames, filled on the MQTT task, drained on displayTask | media_target/PcMediaAdapter.{h,cpp}, media_target/MediaMsgQueue.h | 5 | MEDIA-05, MDL-03 | 100k host frames stay ordered with a correct drop count; device log reads `dropped 0` after a 10-minute session |
| MEDIA-07 | Volume coalescing (≤1/120ms + a 200ms settling call) + generation-counter echo suppression | media_target/PcMediaAdapter.cpp | 4 | MEDIA-06, MDL-12 | 100 detents in 200ms → ≤3 publishes, the last one final (up to 40/s today, echoed back) |
| MEDIA-08 | Idempotent `play`/`pause` gated on the model, plus a `toggle` verb with expiring suppression | media_target/PcMediaAdapter.cpp, media_target/PcProtocol.{h,cpp} | 3 | MEDIA-05 | Two taps start then stop, never double-toggle |
| MEDIA-09 | `seek_abs:<pct>` / `seek_rel:<±ms>` encoders, default-closed on learned caps | media_target/PcProtocol.{h,cpp}, PcMediaAdapter.cpp | 3 | MEDIA-04, MEDIA-05 | With no v2 reply seen, nothing is published; after one v2 reply it publishes |
| MEDIA-10 | `request:v2` with v1 fallback: populate the full model, or stay v1 with position unknown | media_target/PcMediaAdapter.cpp, PcProtocol.{h,cpp} | 4 | MEDIA-05, MEDIA-09 | Against the old PC app: volume and play-state show, the position bar hides rather than showing 0 |
| CTRL-14 | Prune SoundController's dead members (`lastDrawnSetpoint`, `colorChange`, `initialized`, two timestamps) and the two empty media-button stubs | SoundController.{h,cpp} | 1.5 | CTRL-07 | SOUND still syncs and toggles; binary delta within the `BLD-38` row |
| MEDIA-16 | Delete `SoundController`, rewiring `DisplayController` and `main` to hold `MediaScreenController`; the MQTT edge stays until Phase 5 | SoundController.{h,cpp}, DisplayController.{h,cpp}, main.cpp | 3 | MEDIA-13, MEDIA-14, CTRL-15, CTRL-14 | `grep SoundController` clean; MEDIA enter / knob / tap / back and the serial commands all work |

**WP3.5 Light (D19: keeps the `p0-fixed` layout)** (5 tasks, 17.0h)

Model bindings: `LightModel::{brightness, hue, saturation, ledMode}`; `KnobModel::{detentCount, linkAlive}`; `ConnectivityModel::mqttLightConfigured`.

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| CTRL-13 | Prune 12 dead LightController members + `handleModeChange` + `constrain<T>`; delete `setpoint`/`lastSetpoint` (**folds MDL-34**) | LightController.{h,cpp} | 2 | CTRL-08 | Binary delta within the `BLD-38` row; LIGHT behaves identically to `p0-fixed` |
| CTRL-21 | `LightView`: build + one render pass via `model::hsvToRgb`; no LVGL lock taken when nothing changed (60 lock/s for the same string today) | ui/screens/LightView.{h,cpp} | 4 | SCRN-03, MDL-09, CTRL-13, UIX-29 | Matches the `p0-fixed` screenshots at three hue/brightness pairs (**D19** — not the ring shell); the lock is untaken on idle frames |
| CTRL-22 | `LightScreenController`: drag origin + thresholds, knob delta, **fix the swallowed back gesture**, settle relative-vs-absolute drag | controllers/screens/LightScreenController.{h,cpp} | 5 | CTRL-21, MDL-12, MDL-16 | Drag → lift → long-press backs out 10/10 (swallowed within 10ms of a drag today); drag-and-hold does not back out |
| CTRL-23 | Decide the LED mode path: bind a cycling gesture, or delete `LedMode` and the `modeControl` publish (§9 Q17) | controllers/screens/LightScreenController.cpp, ui/screens/LightView.cpp | 3 | CTRL-22 | Either the label cycles with a matching publish, or `grep LedMode` is empty |
| CTRL-24 | Delete `LightController`, rewiring `DisplayController` and `main`; the existing publish callback stays until `APP-15` | LightController.{h,cpp}, DisplayController.{h,cpp}, main.cpp | 3 | CTRL-22, CTRL-23 | `grep LightController` clean; LIGHT end-to-end including broker traffic |

**WP3.6 Knob write path and liveness (plan 3.7, part 2)** (6 tasks, 16.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| PCKNOB-15 | Open the write path: `setSetpoint(int)` sending `setpoint=N\n`; delete the never-called `askSetpoint()` | KnobController.{h,cpp}, model/KnobModel.cpp | 3 | INPT-22, INPT-23, SCRN-04 | An 80→20 mode switch logs `Updated setpoint: 20` on the knob and the next detent starts from 20 |
| PCKNOB-16 | Knob acks with `ok:setpoint=N`; the screen keeps an in-sync flag; accept both `=` and `:` | serial.ino, KnobController.{h,cpp} | 2 | PCKNOB-15 | Ack within 100ms of screen entry; unplugging the knob flips the flag |
| PCKNOB-17 | Screen-initiated `ping`/`pong:<ms>` liveness into `KnobModel` | serial.ino, KnobController.{h,cpp}, model/KnobModel.cpp | 4 | PCKNOB-09, MDL-28, PCKNOB-15 | Round trip logged; unplug marks the link dead within the timeout; light sleep is still entered |
| PCKNOB-19 | Validate and clamp the four haptic setters (`poles=0` NaN-poisons the PID today) | serial.ino, FOC.ino | 2 | — | Four out-of-range values rejected; detents keep holding afterwards |
| PCKNOB-20 | Fix the `calibrate` passthrough name; delete the unreachable `zeroPos` and `sp?` handlers | serial.ino | 1 | PCKNOB-04, PCKNOB-15 | `calibrate` reaches a handler; `grep zeroPos` and `grep "sp?"` empty |
| INPT-25 | Push `KnobModel` haptic config to the knob plus a bench command | input/InputRouter.{h,cpp}, model/KnobModel.cpp, CommandHandler.{h,cpp} | 4 | PCKNOB-15, PCKNOB-19 | Knob logs `Updated poles:`; detent spacing changes; out-of-range values rejected in the host tests |

**WP3.7 PC protocol v2 and the PC app** (14 tasks, 44.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| PCKNOB-21 | Specify PC media protocol v2: verbs, state reply, **encoding chosen explicitly** (§9 Q6), 1024B cap | media_target/pc_media_protocol.md | 2 | PCKNOB-04 | Every field typed, ranged and truncation-ruled, each traced to a `NET-01` capture |
| PCKNOB-22 | PC: accept `seek_abs`/`seek_rel`; add `seek_to_percent`/`seek_by_ms` | PC/src/*.py, PC/tests/** | 4 | PCKNOB-06, PCKNOB-21 | pytest covers 0/50/100 and ±5000 plus rejection; the verb appears in the app log |
| PCKNOB-23 | PC: digit-key seek interim behind a `seek_backend` flag (focus, digit, restore focus) | PC/src/media_controller.py | 3 | PCKNOB-22 | An unfocused tab jumps to 40% and focus returns; volume never steals focus |
| PCKNOB-24 | PC: publish the v2 state reply (state, volume, position, duration, title, player, caps) with the title last | PC/src/*.py, PC/tests/** | 3 | PCKNOB-21, PCKNOB-06 | A title containing a comma and a colon round-trips; six fields present |
| PCKNOB-25 | PC: replace the `- YouTube` play heuristic; actually use the window cache (`EnumWindows` runs every poll today) | PC/src/media_controller.py | 3 | PCKNOB-24 | A paused tab reports False; `EnumWindows` runs ≤1 per interval |
| PCKNOB-26 | Browser extension (MV3): `seekTo`, `getCurrentTime`, `getDuration`, `getPlayerState`, title | PC/extension/** | 6 | PCKNOB-21 | `seekTo(30)` works with the tab backgrounded; a missing API no-ops instead of throwing |
| PCKNOB-27 | PC: loopback-only WebSocket bridge with a first-run token, off the Tk loop | PC/src/browser_bridge.py, PC/tests/** | 6 | PCKNOB-06, PCKNOB-26 | A tokenless client is refused; `netstat` shows no LAN-interface bind |
| PCKNOB-28 | PC: route seek and state through the bridge with the digit fallback; the reply states which path served it | PC/src/*.py | 4 | PCKNOB-23, PCKNOB-27, PCKNOB-24 | The bridge lands within 1s of 37%; the fallback lands on 30% and says so |
| PCKNOB-37 | Delete `media.controller.py`; wire or delete the five dead UI paths | PC/src/*.py | 2 | PCKNOB-01, PCKNOB-24 | "Detected Player" shows a real name; `import media_controller` works |
| PCKNOB-38 | Fix the play/pause toggle misuse; stop the UI asserting a state it does not know | PC/src/*.py | 3 | PCKNOB-25, PCKNOB-28 | `pause` twice leaves it paused; the label is honest |
| PCKNOB-39 | Break the PC-side volume echo loop; make `last_volume` thread-safe | PC/src/windows_audio_controller.py, PC/src/*.py | 3 | PCKNOB-01 | A 20→80 sweep sends one setpoint per detent out and zero echoes back |
| PCKNOB-40 | Make the LAN sweep explicit; snapshot the port; bind the loop variable | PC/src/tcp_subtab.py | 2 | PCKNOB-01 | No connections attempted until Scan is pressed; progress counts correctly |
| PCKNOB-41 | Parse the real `Device name: <x>` reply instead of adopting a boot-log line | PC/src/serial_subtab.py | 2 | PCKNOB-01 | Status reads `Connected to QNOB on COMx` even while boot logs stream |
| PCKNOB-42 | Resolve `qnob_config.json` next to the app, with a one-time CWD migration | PC/src/config_handler.py | 1.5 | PCKNOB-02 | Two launch directories read one config; settings persist |

**WP3.8 Spotify** (21 tasks, 87.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| MEDIA-17 | `SpotifyHttp`: one request at a time, cert bundle, 8s timeout, **per-instance** buffer, cleanup on every path | spotify_client/SpotifyHttp.{h,cpp} | 5 | MEDIA-02, APP-02 | `status=200 len=N` with N inside the buffer, or an explicit truncation warning |
| BLD-13 | Register `components/spotify_client` (REQUIRES model media_target; PRIV_REQUIRES esp_http_client esp-tls json storage) | spotify_client/CMakeLists.txt | 2 | BLD-12, MEDIA-17 | Resolves with no new managed dependency; a 200 on device |
| APP-10 | Spotify/media-target NVS keys ≤15 chars (`spf_client_id`, `spf_refresh_a/b`, `spf_slot`, `spf_expiry`, `media_target`) | NVSManager.{h,cpp} | 3 | — | `listNVSValues` shows the id and a masked token, surviving a power cycle |
| APP-11 | Crash-safe two-slot token rotation (write inactive → commit → flip → commit) + write-if-changed, ending the 10-commits-per-`saveWiFiCredentials` pattern | NVSManager.{h,cpp} | 5 | APP-10 | A forced reset between commits still reads back a valid token; commit count per save drops to 1 |
| APP-12 | `configureSpotify:<id>:<token>`, serial/TLS only, never echoed, with a re-auth hook | CommandHandler.{h,cpp} | 4 | APP-03, APP-10, MEDIA-20 | Serial replies `OK` with only `len=`; plaintext MQTT is refused |
| APP-13 | Persist the active media target through the model (NVS subscribes; it never writes in parallel) | CommandHandler.{h,cpp}, NVSManager.{h,cpp} | 2 | MDL-03, APP-10 | `setMediaTarget:spotify`, reboot, boot log confirms before the first poll |
| BLD-18 | Decide at-rest protection for the refresh token: flash+NVS encryption, or a signed-off risk (§9 Q10) | sdkconfig.defaults, partitions.csv, dev_notes | 3 | APP-11 | `read_flash` shows no cleartext token, or dev_notes carries the signed decision |
| MEDIA-20 | `SpotifyAuth::refresh()` PKCE; **persist a rotated refresh token before using the new access token**; 60s skew; terminal `invalid_grant` | spotify_client/SpotifyAuth.{h,cpp} | 6 | MEDIA-17, APP-10, APP-11 | `rotated refresh token persisted` precedes `access token valid`; survives a power cycle mid-refresh |
| MEDIA-21 | `SpotifyAdapter` endpoints; 200 and 204 both success; no `device_id` sent | spotify_client/SpotifyAdapter.{h,cpp} | 6 | MEDIA-20, MEDIA-03 | A knob turn changes a phone's volume in <1s with `PUT volume -> 204` |
| MEDIA-22 | `parsePlayerState` via cJSON into fixed buffers; 204 is a valid empty state; absent keys leave fields untouched | spotify_client/SpotifyStateParser.{h,cpp} | 6 | MEDIA-17, MEDIA-06, UIX-10 | Three fixtures parse with a zero allocation delta |
| MEDIA-23 | One `mapStatus`: 401 refresh-retry, 403 Premium latch, 404 no-device, 429 `Retry-After` backoff, 5xx retry — **writing only the Spotify `availability` slot** | spotify_client/SpotifyError.{h,cpp} | 5 | MEDIA-21, MEDIA-22, UIX-10 | Status table asserted; a Free account logs one `PremiumRequired` with no storm; the PC slot is untouched |
| MEDIA-24 | `SpotifyPoller` gated on MEDIA active **and** Spotify selected; 5s/15s/0 cadence; suppressed during backoff | spotify_client/SpotifyPoller.{h,cpp} | 4 | MEDIA-23, MEDIA-13 | Silent within one interval after leaving the screen or switching target |
| MEDIA-25 | All Spotify HTTPS off displayTask, fixed request queue, stack sized from the measured high-water | spotify_client/SpotifyTask.{h,cpp}, main.cpp | 5 | MEDIA-21, MEDIA-06 | No LVGL gap >50ms during a command; ≥2KB stack headroom |
| MEDIA-26 | Heap budget for the second TLS session: trim buffers, keep-alive, teardown when polling stops | spotify_client/SpotifyHttp.cpp, sdkconfig.defaults.esp32c6 | 4 | MEDIA-25, APP-02, BLD-38 | `HEAP with MQTTs+Spotify` recorded; largest free block above the `BLD-38` floor (§9 Q3) |
| MEDIA-27 | Host tests for the parser and error mapper against 8 checked-in fixtures | spotify_client/test/**, test/host/test_state_parser.cpp, test_error_map.cpp | 5 | MEDIA-22, MEDIA-23 | Green; a truncated fixture yields `BadResponse` under ASan |
| PCKNOB-32 | PC: OAuth PKCE, loopback-only callback (`127.0.0.1`, not `localhost`), S256, `state` checked | PC/src/spotify_auth.py, PC/tests/** | 6 | PCKNOB-02, PCKNOB-06 | Challenge equals S256(verifier); the listener binds loopback only; no token in any log |
| PCKNOB-33 | PC: Spotify provisioning UI + Premium check; fix the Configure tab's hardcoded index 2 in five places | PC/src/*.py | 4 | PCKNOB-32 | The Configure tab still enables correctly after a tab reorder; a Free account is reported before anything is sent |
| PCKNOB-35 | PC: refuse credentials over unverified MQTT; unify the split `PC`/`QNOB` sender identity | PC/src/*.py | 3 | PCKNOB-32, APP-12 | Plaintext refused with nothing published; TLS or serial answers OK |
| PCKNOB-36 | PC: re-provisioning triggered only by a device-reported `invalid_grant` | PC/src/*.py | 3 | PCKNOB-33, MEDIA-20 | A simulated `invalid_grant` prompts once; 429 and 500 do not |
| BLD-16 | Move the cert bundle to shared defaults (C6-only today); add mbedTLS dynamic buffers for a second session | sdkconfig.defaults, sdkconfig.defaults.* | 3 | BLD-13 | Three variants build; MQTTs and Spotify both return 200 in one session with heap logged |
| BLD-17 | Gate the Spotify work on the two-TLS heap measurement; confirm the draw buffer does not step down | LvglDisplay.cpp, spotify_client/**, dev_notes | 3 | BLD-16, BLD-03, BLD-38 | No `buffer A alloc failed` during a request; the draw-buffer line count equals the `BLD-38` required value |

**WP3.9 Peripherals and audio** (8 tasks, 21.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| PERI-13 | `Speaker::write`: fixed scratch buffer instead of a per-call `std::vector`; fix the odd-length truncation that reports `ESP_FAIL` | local_audio/Speaker.{h,cpp} | 2 | PERI-11 | Free heap unchanged across one effect; an odd-size WAV completes OK |
| PERI-14 | Stop re-running the full ES8311 bring-up (~40 I2C writes) per differently-formatted clip on the shared touch bus | local_audio/Speaker.{h,cpp}, SoundPlayer.cpp | 3 | PERI-13 | Five rapid swipes play with no dropped effect and no codec write errors |
| PERI-15 | Serialise Speaker and Microphone on the shared I2S port and codec (single owner) | local_audio/Speaker.{h,cpp}, Microphone.{h,cpp}, LocalAudio.cpp | 4 | PERI-11, PERI-14 | A tap during a recording logs the skip: no I2S error, no crash |
| PERI-16 | Resolve the ES8311 capture path, or make recording fail loudly on ES8311 boards (§9 Q19) | local_audio/Speaker.cpp, Microphone.cpp, SoundRecorder.cpp | 4 | PERI-15 | `[ES8311-capture]` a 3s recording contains non-zero samples and playback still works; or recording refuses with a clear error |
| PERI-17 | Delete the boot-time `/sdcard/esp32_test.txt` write and the commented `logDirectoryTree` calls | SDCard.cpp | .5 | — | No `Test file written` line; mount lines intact |
| PERI-18 | Arbitrate SD against panel flushes on SPI2 (or adopt a no-SD-while-active policy); stop swallowing `INVALID_STATE`; stop `spi_bus_free` on a bus it may not own (§9 Q2) | SDCard.{h,cpp} | 3 | PERI-17 | An SD WAV during the INFO animation ×20: no corruption and the mount survives |
| PERI-19 | Make SoundPlayer's optional-SD contract explicit (one guarded accessor); add the missing `displayController->update()` null guard | SoundPlayer.{h,cpp}, main.cpp | 2 | PERI-12 | A no-SD boot still plays effects and the chime, with no null dereference |
| BLD-21 | Wire `QNOB_ENABLE_KNOB_UART` / `QNOB_KNOB_BAUD_RATE`; default off where pins are −1; record the IMU/LCD pin clash | board_hal/Kconfig, hal_config.h, sdkconfig.defaults.esp32s3_amoled175, main.cpp | 3 | PCKNOB-03 | `[S3-1.75]` logs no UART failure (constructed with −1 pins today); the C6 baud rate comes from menuconfig |

### Phase 4 — Router replaces DisplayController (29 tasks, 76.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| DISP-08 | Replace the global `enum Mode` with `enum class AppScreen` (**D8** — no SLEEP member); port `modeToString`; fix the three external users | model/AppScreen.h, DisplayController.{h,cpp}, CommandHandler.cpp | 2.5 | INPT-14, MDL-26 | Build clean with no identifier `Mode` outside `ModeController`; the renamed log line appears |
| DISP-11 | `ScreenRouter`: `AppScreen` state + a fixed controller array; port the 140-line switch as route→tick→drain; `setMode`→`navigate` | controllers/ScreenRouter.{h,cpp} | 7 | SCRN-04, SCRN-08, SCRN-03, DISP-08 | The full navigation graph twice including boot handover: one `Changing screen` per hop, one `onEnter`/`onExit` each |
| DISP-12 | Convert the five flag pairs into `NavigationRequest`s and delete them from the screens | controllers/ScreenRouter.cpp, ui/views/EnvironmentView.cpp, ui/views/DeviceInfoView.cpp | 4 | DISP-11, SCRN-20 | The arc-shrink leave animation still completes before HOME appears |
| DISP-13 | `ScreenRouter` owns the single unconditional LVGL pump; delete all seven in-view pumps | controllers/ScreenRouter.cpp + 6 screen/view files | 3 | DISP-11 | ≈33 pumps/s on every screen (twice on INFO first paint and zero elsewhere today); no banded wipe; no `lv_timer_handler` re-entry under the lock |
| DISP-14 | `DisplayPowerController` as the sole owner of panel power, the inactivity stamp and `registerActivity` forwarding | display/DisplayPowerController.{h,cpp} | 3 | DISP-11 | `Display turned off` exactly once at the timeout; a touch restores the previous screen (**D8**) in one frame |
| DISP-15 | **D7** collapse the two independent 30s display-power owners; keep the pre-sleep callback | display/DisplayPowerController.cpp, main.cpp, SleepHandler.cpp | 2.5 | DISP-14, PERI-09 | Four stages fire off one clock at 20s/30s/120s plus a wake with the CPU restored; exactly one display-off line |
| DISP-17 | Delete `transitionToMode()` (an all-empty switch) and `CALIBRATE_ORIENTATION`; keep the command as a logged rejection | DisplayController.{h,cpp}, CommandHandler.cpp | 1.5 | DISP-08 | `grep CALIBRATE_ORIENTATION` empty; `calibrateOrientation` leaves the UI responsive |
| DISP-18 | Delete the unread `DisplayBackendSelection` API, keeping the factory seam | DisplayBackendFactory.{h,cpp} | 1 | — | `[S3-1.28]` and C6 both build; the backend boot log is unchanged |
| DISP-22 | Stop the `const_cast` in-place byte swap of LVGL's draw buffer (staging buffer or `LV_COLOR_16_SWAP`) | EspLcdSh8601DisplayFlushBackend.cpp, LvglDisplay.cpp | 2 | DISP-01 | Colour fidelity on the INFO arcs on both panel backends; DMA heap not regressed against `BLD-38` |
| DISP-28 | Delete `DisplayFlushBackend::endWrite()` from the interface, both overrides and its two call sites; delete the GC9A01 `ensureSwapBuffer`/`swapBuffer*`/`txCommand`/`setAddressWindow` block (unreachable on QSPI) | DisplayFlushBackend.h, EspLcdSh8601DisplayFlushBackend.cpp, EspLcdGc9a01DisplayFlushBackend.cpp, LvglDisplay.cpp | 1.5 | DISP-22 | Both panel configurations build and flash; binary delta within the `BLD-38` row; backend boot log unchanged (SRCS exclusion is §9 Q35) |
| DISP-21 | Fix `LvglDisplay::init()` failure paths: free all three buffers, guard `lv_init()` against re-entry | LvglDisplay.cpp | 1.5 | DISP-01 | A forced failure then a success leaves DMA-internal free unchanged, with no assert |
| DISP-23 | Clamp the even-alignment rounder to panel bounds | LvglDisplay.cpp | 1 | DISP-01 | A host helper never exceeds bounds for widths 465–467; C6 output unchanged |
| DISP-20 | `pauseTicks()`/`resumeTicks()` so LVGL ticks stop while the panel sleeps | LvglDisplay.{h,cpp}, display/DisplayPowerController.cpp | 1.5 | DISP-14 | Arcs resume from where they were rather than appearing already complete |
| SCRN-16 | Retire `ModeController`, removing the second `Mode` enum, the `setActive` plumbing and three dead predicates | ModeController.{h,cpp}, DisplayController.{h,cpp}, main.cpp | 2 | SCRN-15, DISP-08, DISP-11 | `grep 'enum Mode'` yields only `AppScreen`; all three return routes work |
| SCRN-23 | First-paint brightness out of the views; the router dims → enters → pumps once → restores the recorded level | ui/views/*.cpp, display/DisplayPowerController.cpp | 3 | SCRN-18, SCRN-10, DISP-11, DISP-14 | No banded repaint on entry; entering a screen during power save does not light the panel; `displayIsOn` stays in sync |
| SCRN-26 | Register the four screen controllers with `ScreenRouter`; delete the drain switch and the eight accessors | DisplayController.{h,cpp}, main.cpp | 4 | SCRN-09, SCRN-12, SCRN-16, SCRN-21, DISP-11 | The `p0-fixed` transition sequence is reproduced with exactly one pump per iteration |
| DISP-19 | Delete `DisplayController` and rewire main + CommandHandler (commands arrive via a queue drained on displayTask) | DisplayController.{h,cpp}, main.cpp, CommandHandler.{h,cpp} | 3 | DISP-11, DISP-12, DISP-13, DISP-14, DISP-17, SCRN-23, SCRN-26 | No `DisplayController` symbol remains; a full flash-and-walk including serial `home`/`info` and idle→wake |
| DISP-24 | Join the startup splash task instead of waiting 30ms | main.cpp | 1.5 | DISP-13, SCRN-09 | Splash exit precedes the first screen change on 20 cold boots |
| CTRL-31 | Media/Light navigation flags → `NavigationRequest`; delete the two drain blocks and their `setActive` side effect | controllers/screens/MediaScreenController.cpp, LightScreenController.cpp, DisplayController.cpp | 2.5 | DISP-11, MEDIA-16, CTRL-24 | A long press returns to the wheel first try; re-entry shows a freshly synced value |
| INPT-28 | Hand inactivity to `ScreenRouter` over `InputRouter::msSinceActivity`; collapse the three timeouts (30s/30s/dead 60s) into one | DisplayController.{h,cpp}, hal_config.h, SleepHandler.cpp | 5 | INPT-14, DISP-11 | One display-off per cycle at the configured timeout; exactly one wake |
| INPT-29 | Route wake through a `NavigationRequest` from `consumeWakeEvent()`, not a mode special case | controllers/ScreenRouter.cpp, input/InputRouter.cpp | 3 | INPT-28, INPT-11 | Tap, swipe and detent each wake to a fully drawn **previous** screen; none leaks a navigation |
| APP-14 | Navigation commands become router events; delete `calibrateOrientation`; drop `PRIV_REQUIRES display` from command_handler | CommandHandler.{h,cpp}, main.cpp, command_handler/CMakeLists.txt | 4 | DISP-11, MDL-03 | `home` over MQTT switches within a frame, drained on displayTask |
| PERI-22 | Restore power state on light-sleep wake (`power_save_enabled` stays set today) | SleepHandler.cpp | 1.5 | DISP-15 | Wake logs exit-power-save, CPU restore and display-on with no further touch |
| PERI-23 | Stop the sleep handler reconfiguring GPIOs it does not own (the knob UART pins 1/2) | SleepHandler.{h,cpp} | 3 | PERI-22, INPT-23 | Detents still arrive after a light-sleep wake |
| PERI-24 | Sleep-inhibit lease taken by OTA and by recording, released on every path | SleepHandler.{h,cpp}, OTAManager.cpp, local_audio/LocalAudio.cpp | 2.5 | DISP-15, PERI-11 | A >200s OTA is not killed at 180s; deep sleep resumes 180s after it finishes |
| PCKNOB-18 | `quiet`/`resume` so the knob is silent while the screen sleeps but the first detent still wakes it | serial.ino, HapticController3.ino, KnobController.{h,cpp} | 3 | PCKNOB-17, PERI-24 | A detent wakes once and is not double-applied; the console is silent while asleep |
| PERI-25 | Delete sleep_handler dead code (`mutex_ptr`, the 8-function C API, five setters two of which write the same field, two predicates, `WAKEUP_PIN_KNOB`) | SleepHandler.{h,cpp} | 1.5 | PERI-23 | Build clean, binary delta within the `BLD-38` row, four stages still logged |
| BLD-22 | Strip `display` back to the panel layer — **drops the cycle edge** `PRIV_REQUIRES controllers ui sleep_handler`, plus `touch_panel` and `knob_controller` | display/CMakeLists.txt, DisplayController.{h,cpp} | 2 | DISP-19, BLD-11 | The `BLD-45` dump shows `display` with no project-local component beyond `board_hal` and the two `esp_lcd_*` drivers, and no cycle |
| BLD-23 | Split `display` into public abstract headers and private backend headers | display/CMakeLists.txt, 8 headers | 3 | BLD-22 | Including a concrete backend header from `ui` now fails to compile |

### Phase 5 — Transport out of views (15 tasks, 52.0h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| BLD-44 | **Declare this phase's component edges**: `adapters` gains `PRIV_REQUIRES mqtt_manager`; `command_handler` gains `model`; `main` keeps one declared command edge | adapters/CMakeLists.txt, command_handler/CMakeLists.txt, main/CMakeLists.txt | 1.5 | BLD-24, BLD-43 | `MqttModelBridge.cpp` compiles inside `adapters`; the `BLD-45` dump shows `mqtt_manager` private to `adapters` |
| NET-15 | Declare the complete topic table in one header, including the four literal `ledRing/*` and the `get_*` topics; delete the two dead `MQTT_TOPIC_LIGHT_*` macros | adapters/MqttTopics.h, MQTTManager.h | 2 | NET-01, BLD-44 | No `esp32/`, `qnob/` or `ledRing/` literal outside the table; the count matches the `NET-01` capture |
| NET-16 | `MqttModelBridge`: owns the subscribe set, routes by exact topic, strips `sender=` and self-drops; fixes the destroyed function-local `broker.address.uri`; stops `ESP_LOGD` printing the broker password | adapters/MqttModelBridge.{h,cpp}, MQTTManager.cpp | 6 | NET-15, MEDIA-05, MDL-03 | Per-topic routing asserted; a third-party `setpoint:30` moves the arc while our own publish does not bounce; no password in any log level |
| NET-17 | Wire the four inbound routes; fix the indoor-temp parse (`std::stof` on a prefixed payload with an empty catch); **actually subscribe the light topic** | adapters/MqttModelBridge.cpp, MqttTopics.h | 5 | NET-16, MDL-18, MDL-20 | One log line per route with the parsed value; malformed payloads log a rejection; `esp32/light/brightness` now moves the fill |
| NET-18 | Outbound publishing becomes model observers reading the seqlock snapshot, with the `MDL-12` rate limits | adapters/MqttModelBridge.{h,cpp} | 5 | NET-16, NET-05, MDL-12 | A full-range knob sweep is bounded to single-digit msgs/s with the final value correct |
| APP-28 | **Persisted runtime fields** (owner request 2026-09-12): a `SettingsPersister` adapter subscribes to `MediaModel`, `LightModel`, `NavigationModel` and `KnobModel` and writes volume, brightness, LED mode, the wheel selection and the haptic config to NVS under keys of at most 15 chars (`rt_volume`, `rt_bright`, `rt_led_mode`, `rt_wheel`, `rt_knob_poles`, `rt_knob_str`, `rt_knob_snap`, `rt_knob_vlim`) with write-if-changed and a 3 s quiet-period debounce per field; at boot the composition posts the stored values as each model's first records. Generalises `APP-13`; the models never call NVS (purity guard) and NVS never writes a model | adapters/SettingsPersister.{h,cpp}, NVSManager.{h,cpp}, app_composition/AppComposition.cpp, test/host/test_settings_persister.cpp | 5 | NET-18, APP-11, APP-13, MDL-16, MDL-26, MDL-28, BLD-06 | Values survive a power cycle and come back as the first records; a 200-detent sweep ends in exactly one commit per field after 3 s of quiet; a host test on `FakeClock` pins the debounce and write-if-changed; 0 allocations on the notify path |
| NET-19 | Reduce the MQTT event callback to enqueue-and-return; replace the file-static instance dispatch with `handler_args` in **both** `MQTTManager` and `WiFiManager` | MQTTManager.{h,cpp}, WiFiManager.{h,cpp}, adapters/MqttModelBridge.cpp | 3.5 | NET-16 | Every model write happens on DisplayTask; `esp32/command` still executes; `grep s_mqtt_manager` and `grep s_wifi_manager` are both empty |
| NET-20 | Resolve the light broker: one client or two (§9 Q7) | MQTTManager.{h,cpp}, adapters/MqttModelBridge.cpp, main.cpp | 4 | NET-19, NET-15 | Heap logged before and after against `BLD-38`; light traffic appears on the chosen broker only |
| NET-22 | One wire-scale conversion for brightness; `LightModel::brightness` stays 0–100 (§9 Q18) | adapters/MqttModelBridge.cpp, MqttTopics.h | 2 | NET-17, MDL-16 | 0/50/100 publish as 0/127/255 and parse back symmetrically (cannot round-trip today) |
| NET-23 | Gate the `esp32/command` route permanently: allowlist, refuse destructive over plaintext, OTA URL TLS-only | adapters/MqttModelBridge.cpp, CommandHandler.{h,cpp} | 4 | NET-17, APP-03 | `clearNVS` over MQTT refused with NVS intact; allowlisted commands execute |
| NET-26 | Delete MQTTManager's dead sound-message path, `unsubscribe`, `getClientName`, the C shim and the `useLightConfig=true` branch | MQTTManager.{h,cpp} | 2 | NET-16, NET-20 | Build clean, binary delta within the `BLD-38` row, MEDIA still round-trips |
| MEDIA-29 | Route `esp32/sound/*` through the bridge; stop subscribing to the two topics the device solely publishes | main.cpp, MQTTManager.cpp, media_target/PcMediaAdapter.cpp | 4 | MEDIA-16, NET-16 | `grep 'find("sound")'` empty; PC volume still reaches the arc with no self-echo; two `esp32/sound/*` messages arriving back to back no longer overwrite one slot |
| APP-15 | Delete the `main.cpp` MQTT lambda and the five screen MQTT callbacks; keep exactly one declared command edge | main.cpp, controllers/screens/*.cpp | 3 | NET-17, NET-18, APP-03, SCRN-21, CTRL-24 | No topic literal anywhere in `main.cpp`; `setMQTTPublishCallback` and friends gone |
| APP-25 | Decide and implement the light MQTT profile: add `configureLightMQTTServer`, or delete the profile and its gate (§9 Q7) | CommandHandler.{h,cpp}, NVSManager.{h,cpp}, main.cpp | 3 | NET-20 | A LIGHT knob turn produces a broker message on the configured broker, provisioned by a real command |
| BLD-25 | Put `skip_cert_common_name_check` behind a dev-only Kconfig (default n) | MQTTManager.cpp, Kconfig.projbuild, sdkconfig.defaults | 2 | BLD-16 | Off: the production broker handshake succeeds and a CN mismatch now fails |

### Phase 6 — Composition root (15 tasks, 44.0h)

`APP-16` is split so neither half sits at the 8h cap and each is independently boot-log-verifiable.

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| APP-16 | `AppComposition` part 1 — drivers and peripherals ownership: `unique_ptr` for the driver/peripheral globals, construction order preserved, `SleepHandler` stays borrowed; register the component | app_composition/AppComposition.{h,cpp}, app_composition/CMakeLists.txt, main.cpp | 5 | APP-06, APP-14, APP-15 | Boot log identical to `p0-fixed` on C6 with and without an SD card; `[S3-1.75]` deferred and recorded if absent |
| APP-27 | `AppComposition` part 2 — views, controllers, router and adapters ownership; keep both conditional teardowns | app_composition/AppComposition.{h,cpp}, main.cpp | 4 | APP-16 | Boot log identical through `Tasks started`; a PWR_KEY reset shows no watchdog backtrace |
| APP-17 | Move the four task bodies and the pre-HAL AMOLED reset behind an explicit early-reset entry point; delete the unreachable null-check-and-realloc of the splash screen | main.cpp, app_composition/AppComposition.cpp, hal_init.{h,cpp} | 6 | APP-27 | `main.cpp` under 100 lines; 10 soft resets and one cold boot all render the splash |
| APP-18 | Replace the 30ms splash pseudo-join with a real bounded handshake | app_composition/AppComposition.cpp, main.cpp | 3 | APP-16, DISP-24 | `splash task joined` precedes router init on 20 boots with no timeout; two tasks can no longer call `lv_timer_handler()` |
| APP-19 | Battery sampling off the display loop into the poller, with a boot sample; add the missing null guard | app_composition/AppComposition.cpp, main.cpp | 2 | PERI-06, APP-16 | A percentage within 1s of boot; no 16ms frame overruns from ADC/I2C |
| APP-20 | Use or delete the four write-only task handles | app_composition/AppComposition.{h,cpp}, main.cpp | 1 | APP-17, APP-18 | `-Wunused-but-set-variable` clean; `tasks` prints high-water marks |
| DISP-25 | Own `ScreenRouter`, `DisplayPowerController` and `InputRouter` in `AppComposition` with explicit order | app_composition/AppComposition.cpp, main.cpp | 2 | DISP-19, APP-27 | Identical boot behaviour; boot heap within the `BLD-38` row |
| CTRL-37 | `unique_ptr` for the Media/Light controllers, `PcMediaAdapter` and `LocalAudio` | app_composition/AppComposition.cpp, main.cpp | 2.5 | APP-27, MEDIA-16, CTRL-24, PERI-11 | Boot heap within the `BLD-38` row; a PWR_KEY reset shows no watchdog backtrace |
| INPT-30 | Composition owns `TouchPanel` and `KnobController`; `InputRouter` is their only holder | app_composition/AppComposition.cpp, input/InputRouter.h | 3 | INPT-14, APP-16 | Touch and UART init logged once each; the wake-on-touch monitor is still configured |
| NET-24 | Move network construction into `AppComposition` with explicit order (event loop before MQTT/SNTP); delete `InternetHandler`'s dead accessors | app_composition/AppComposition.cpp, main.cpp, InternetHandler.{h,cpp} | 4 | NET-06, NET-07, NET-16, APP-16 | Boot sequence unchanged through `Tasks started`; WiFi and MQTT both connect |
| APP-29 | **One writer for the settings store** (owner request 2026-09-12): `WiFiManager` stops writing `NVSManager`; today it stores the Kconfig default into slot 0 and saves `lastConnectedNetworkIndex` from the network task while `connectWifi` writes the same `std::string` members from the command task. It reports the connected slot through an atomic the composition consumes, every NVS write goes through one owner on one task, and the settings struct handed to the drivers is read-only after boot | WiFiManager.{h,cpp}, NVSManager.{h,cpp}, CommandHandler.{h,cpp}, app_composition/AppComposition.cpp | 3 | NET-24, NET-10, APP-11 | `grep saveWiFiCredentials` finds one caller; `connectWifi` during an in-flight connect sequence neither corrupts a slot nor blocks; `lastConnectedNetworkIndex` still follows a reconnect |
| PERI-26 | Peripheral ownership (8 globals) into `AppComposition`; replace the unsynchronised lazy `SleepHandler` singleton; clear `Speaker`'s file-static codec handle in `deinitialize()` | app_composition/AppComposition.{h,cpp}, SleepHandler.{h,cpp}, local_audio/Speaker.cpp | 4 | PERI-06, PERI-11, DISP-14, APP-16 | Full boot sequence in order; boot heap within the `BLD-38` row; a second init after deinit succeeds |
| PERI-27 | Make `OTAManager::lastError` atomic; refuse destruction while busy; delete `setServerCert`/`setAllowInsecure` | OTAManager.{h,cpp} | 1.5 | PERI-26 | `otaStatus` reports the same error the OTA task logged; OTA end-to-end |
| BLD-26 | Reduce `main` to `SRCS main.cpp REQUIRES app_composition`; tighten `app_composition`'s own reqs | main/CMakeLists.txt, app_composition/CMakeLists.txt | 2.5 | APP-17, BLD-24 | `main` has exactly one req; boot log matches `p0-fixed` on all three variants |
| BLD-27 | Drop the redundant lvgl pin from main's manifest; pin an exact version in `ui` | main/idf_component.yml, ui/idf_component.yml | .5 | BLD-26 | The lock still resolves 8.4.0, listed once; three variants build |

### Phase 7 — CMake tightening and cleanup (15 tasks, 41.0h)

Phase 7 only **tightens** REQUIRES; every edge a component needs was already declared by `BLD-41`/`BLD-42`/`BLD-43`/`BLD-44`, so nothing here is the first time a component can reach a header.

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| BLD-28 | Tighten every `REQUIRES`/`PRIV_REQUIRES` to the target layering in one pass — 24 component CMakeLists + main; `controllers` declares the `esp_timer`/`lvgl` it already includes | all CMakeLists.txt | 6 | BLD-22, BLD-24, BLD-26, BLD-44, INPT-21, PERI-11 | `fullclean` + build on all three variants; the `BLD-45` dump shows no lower→higher project-local edge |
| BLD-29 | Make command_handler's deps private; drop `controllers` and `display`; add `model` + `storage` | command_handler/CMakeLists.txt, CommandHandler.cpp | 2 | APP-14, BLD-13 | Empty public reqs; `configureMQTTServer`, `reset` and `configureSpotify` still respond |
| BLD-30 | **Gating** acyclic + layer check over `project_description.json`, called from both build scripts | check-component-graph.sh, build-variant.{sh,ps1} | 3 | BLD-28, BLD-45, BLD-03 | Exits 1 against each of the three `dev_notes/graph-baseline-<variant>.json` files archived by `BLD-03`, and 0 against the current graph, for all three variants |
| BLD-31 | Replace the legacy `driver` umbrella with specific `esp_driver_*` per component | 5 CMakeLists.txt | 2.5 | BLD-28 | All variants build; `driver` gone from every first-party req |
| BLD-32 | Wire or delete the nine dead Kconfig options | board_hal/Kconfig, Kconfig.projbuild | 3 | BLD-21, APP-14 | Every remaining symbol has a reader; changing the inactivity option visibly changes sleep timing |
| BLD-33 | Purge no-op/undefined sdkconfig symbols: SPIRAM, `SOC_WIFI_HE_SUPPORT`, `ENABLE_WIFI_STATION`, **`OPENTHREAD_ENABLED`**, `OTA_ALLOW_HTTP` | sdkconfig.defaults, sdkconfig.defaults.* | 1.5 | — | Clean reconfigure with no unknown-symbol warnings; only intended diffs |
| BLD-34 | Move OTA-over-HTTP into a dev-only overlay | sdkconfig.defaults.dev, build-variant.{sh,ps1} | 1.5 | BLD-33 | The default build refuses an `http://` OTA URL; the dev overlay accepts it |
| BLD-35 | Decide and document whether `managed_components` stays vendored, including `screen-test`'s copy | .gitignore, esp-idf/README.md | 2 | BLD-04, BLD-27, BLD-37 | A clean clone builds and host-tests under the chosen policy |
| BLD-36 | `verify-all.{sh,ps1}`: host tests + purity guard + three variants + graph check + **the `BLD-39` §1/§7 roll-up check** | verify-all.{sh,ps1} | 3 | BLD-09, BLD-30, BLD-39 | Exits 0 on a clean tree; non-zero on any test failure, a reintroduced cycle, or a desynchronised §1 total |
| INPT-32 | Prune the dead TouchPanel surface (4 accessors, `long_hold` + its 10s threshold, the duplicate `gesture`, 3 unread pins, 2 unreachable `first_*` branches, 3 unused gesture values) and note that `reset()` compiles out on C6 | TouchPanel.{h,cpp} | 3 | INPT-20 | A cold boot with a finger on the glass causes no navigation; binary delta within the `BLD-38` row |
| INPT-33 | Stop the wasted per-poll gesture-register read (3 I2C transactions → 2 at 60Hz) | TouchPanel.{h,cpp} | 2 | INPT-08 | Every gesture still works; drag coordinates still smooth; the counted transaction rate drops |
| INPT-34 | Optional: gate touch polling on the TCA9554 INT line with a slow fallback while pressed | input/InputRouter.cpp, TouchPanel.{h,cpp} | 5 | INPT-08, INPT-23 | Idle I2C near zero with hold and drag unchanged; idle current measured and recorded |
| NET-27 | Delete WiFiManager's dead scan/AP-STA/callback/C-shim surface | WiFiManager.{h,cpp} | 2 | NET-06, NET-19, BLD-28 | Build clean; WiFi associates and recovers after an AP power cycle |
| APP-22 | Delete the dead storage surface (C shim, TCP setters + 4 members + 4 keys, 2 info getters), harden `readInt` (no stale out-param, no reused `port` variable), stop `readString` heap-allocating a raw `char[]` per read, **and erase the four orphaned `snd_tcp_*`/`lgt_tcp_*` keys once at boot** (§9 Q30) | NVSManager.{h,cpp}, Kconfig.projbuild | 2.5 | — | Binary delta within the `BLD-38` row; `listNVSValues` byte-identical; a hard NVS error no longer copies the previous value; the orphaned keys are gone after one boot |
| APP-23 | Resolve the dead/duplicated commands (`showNetworks`, `commInfo`, the three overlapping readouts, `Command::name`) (§9 Q23) | CommandHandler.{h,cpp} | 2 | MDL-22, APP-14 | `help` lists only implemented commands; the PC provisioning round trip still completes |

---

## 4. UI work packages (from the winning "Instrument" design)

Shell first, then one package per screen. Every package lists its view tasks, controller tasks and model bindings. All 33 tasks here are **Phase 3**; the design's other three tasks live in §3 — `UIX-01` and `UIX-02` in Phase 1 WP1.0 (they are config, and the theme flip must precede `p0-fixed`'s screenshots) and `UIX-10` in Phase 1 WP1.3 (a model amendment, and the plan forbids retrofitting model decisions). `SCRN-16` and `SCRN-23` are in Phase 4 with `DISP-11`.

Geometry is the design's, which is `EnvironmentInfoScreen.cpp:203-209` re-derived at 466: centre (233,233), R1 420×420 / width 25 (r 185–210, centre-line 198), R2 354×354 / width 17 (r 160–177, centre-line 168), R3 304×304 / width 11 (r 141–152, centre-line 146), disc 264 (r 132), sweep `rotation 120` + `bg_angles(0,300)`, 60° dead wedge on 6 o'clock.

**WP4.0 Shared shell, typography and config — the judges' blocking issues land here** (13 tasks, 32.5h)

| ID | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|
| SCRN-05 | **D10** type ramp `ui/theme/Typography.h` (Display 48 / Title 28 / Label 20) + sdkconfig 20/28/48, drop 14, default 20; **pin `lv_font_montserrat_14` explicitly on the labels of the four not-yet-split screens in the same commit** (each pin deleted by that screen's split task); measure flash per face against the `BLD-03` table | ui/theme/Typography.h, sdkconfig.defaults*, EnvironmentInfoScreen.cpp, ModeController.cpp, SoundController.cpp, LightController.cpp | 3.5 | SCRN-01, UIX-01, BLD-03, BLD-38 | Per-face flash delta recorded against the `BLD-38` row (not against the deleted March binary); the boot logo is visibly larger than the percent label; the four unsplit screens are pixel-unchanged; draw-buffer line count unchanged |
| UIX-03 | Re-lay out InitializationScreen and DeviceInfoScreen for the 20px default (~43% growth on unstyled labels) | InitializationScreen.cpp, DeviceInfoScreen.cpp | 3 | SCRN-05 | Neither screen overflows; DeviceInfo's five rows still fit inside r=200 |
| UIX-04 | Ring-shell module: centre (233,233), R1 420/25, R2 354/17, R3 304/11, disc 264, 120°/300° sweep, `normalizeAngle`, **every `bg_angles` pair emitted pre-normalised**, enter (260ms / 700ms on ambient screens) and leave (200ms) helpers, **and the bottom-wedge slot as a declared metric (centre dy=+150, 188px budget, ≤12 chars, one line) with an assertion over every legend literal** | ui/theme/RingShell.{h,cpp}, ui/theme/Metrics.h | 5 | SCRN-03, SCRN-05 | Geometry derives from `scalePx` and still renders on a 240px board; no raw angle >359 reaches `lv_arc_set_bg_angles`; a 13-character legend literal fails the build or the host check (the design's own `"TURN \| DBL TAP"` is 14 and must be re-cut) |
| UIX-05 | Cursor/cap contract: one 33px cap diameter everywhere (pad_all 4 on 25px, 6 on 21px, 8 on 17px), `LV_PART_KNOB` `bg_opa=COVER` + `radius=LV_RADIUS_CIRCLE` set **explicitly after `remove_style_all`**, `LV_PART_INDICATOR` `arc_width` set even when the indicator is transparent, **one white cursor everywhere** + a 19px grey ghost sharing the cached radius, `LV_CIRCLE_CACHE_SIZE` decision | ui/theme/RingShell.{h,cpp}, sdkconfig.defaults | 3 | UIX-04 | The cap measures 33px on the ring centre-line on every screen including receded rings; both dots share one cached radius; the cache decision (keep 4 or raise to 8) recorded with draw numbers |
| UIX-08 | Measured title ladder (`lv_txt_get_size` once per title change, never per frame): 48 → 28 → 28-wrap → `LONG_DOT`, `LONG_SCROLL_CIRCULAR` as the **last** rung at 20px/s with a 1200ms pause; re-measure every 48px string against the real `adv_w` table (digit 32.0, M 45.8, uppercase ≈35px) | ui/theme/TextFit.{h,cpp} | 2.5 | SCRN-05 | No measurement call in any frame path; a 40-character title renders with no marquee; `CLIMATE` and `SPOTIFY` measured (≈222px and ≈235px), not estimated |
| UIX-09 | Ring invariants: value-zero hides `INDICATOR` and keeps `MAIN` at full opacity, value-unknown drops `MAIN` to `LV_OPA_20` as a dead groove; live-ring triple encoding (opacity + cap + coloured legend); **`shadow_width 0` on every arc `INDICATOR` and never animate the disc gradient concurrently with a ring sweep** (both caches are 0); never animate `arc_width`; `lv_meter` banned for anything that moves; inward-growth clearance rule; the `lv_roller` rejection note | ui/theme/RingShell.h, dev_notes/ring-invariants.md | 2.5 | UIX-04 | A dead groove is visually distinct from a zero value on all three rings; `grep` finds no arc shadow and no concurrent disc+ring animation; the ModeWheel 33px selected segment is checked against R2's r_out 177 |
| UIX-11 | **D5** knob accumulator in `KnobModel`: divisors (4 per wheel step, 3 per target toggle, 1 per volume unit, 1 per 0.5°C, 1-or-3 per seek percent), reset on direction reversal and after 600ms idle, burst→delta clamp ±10, `onEnter` push | model/KnobModel.{h,cpp} | 2 | MDL-28, MDL-33 | One host test per divisor; one implementation with five callers; `grep` finds no second accumulator in any controller |
| UIX-12 | Visible self-healing: overlay 4000ms auto-cancel rendered as a draining R3 ring refilled by any input, SEEK 6000ms auto-revert, `linkAlive=false` forces VOLUME and drops every cap; no modal survives a sleep | controllers/screens/*.cpp, ui/screens/*.cpp | 2 | UIX-04, UIX-11 | Each timeout is visible on screen and in the log; power-saving with the overlay open destroys it and leaves the target unchanged |
| UIX-13 | **D14** swipe-left = `next()`, swipe-right = `previous()` on the media screens; reconcile INFO's swipe-right-is-back in the same pass | controllers/screens/*.cpp, EnvironmentInfoScreen.cpp | 2 | SCRN-25 | Adjacent screens no longer contradict; one swipe = one skip; INFO's release no longer navigates on its own |
| UIX-28 | **Publish the media gesture table** (tap, double-tap, four swipes, rim/rail tap, 3s hold) for MediaSpotify, MediaPC, MediaSeek and the target overlay; resolve double-tap against the design's immediate tap-to-play/pause; specify R3 rail-tap overlay entry and the suppression of the two toggles a commit double-tap would otherwise produce | dev_notes/media_gesture_table.md, controllers/screens/MediaScreenController.{h,cpp} | 2 | UIX-06, UIX-13, MEDIA-13 | One row per (screen, region, gesture) with no cell doing two things; a commit double-tap changes target and leaves playback state unchanged |
| UIX-14 | Three mode glyphs as `LV_IMG_CF_ALPHA_8BIT` compiled C arrays (64×64, 4KB each) with runtime `img_recolor` | ui/assets/icon_light_64.c, icon_climate_64.c, icon_media_64.c | 2 | SCRN-05 | One asset serves lit and dim; 12KB flash delta recorded against `BLD-38` (§9 Q16) |
| UIX-15 | Local position extrapolation (advance `positionMs` at a 250ms cadence while playing, resync on poll, suspended while scrubbing) **plus a 1500ms inbound freeze after `seekTo()`** | media_target/PcMediaAdapter.cpp, controllers/screens/MediaScreenController.cpp | 2.5 | UIX-10 | Progress never steps once per poll; a completed seek is not snapped back by the next poll |
| UIX-29 | Record **D19**: LIGHT is exempt from `UIX-04`/`UIX-05`; `LightView` reproduces the `p0-fixed` layout and `CTRL-21`/`CTRL-22` are accepted against that, not against the ring shell | dev_notes/light-screen-decision.md | .5 | UIX-04, BLD-40 | The decision is written with its reason (no Light screen in the winning design) and `CTRL-21`'s acceptance criterion names the `p0-fixed` screenshots |

**WP4.1 ModeWheel** — replaces `ModeController`; resolves the `Mode` enum collision; TEMPERATURE→CLIMATE, SOUND→MEDIA. The deletion (`SCRN-16`) is in Phase 4. (5 tasks, 12.0h)

| ID | Kind | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|---|
| SCRN-13 | view | `ModeSelectView`: three R1 wedges (LIGHT 120–217.5, CLIMATE 222.5–317.5, MEDIA 322.5–420→normalised 60), R2 selection band, disc glyph + mode word, wedge legend | ui/views/ModeSelectView.{h,cpp} | 3 | SCRN-03, UIX-04, UIX-14, MDL-26 | Title cycles LIGHT→CLIMATE→MEDIA at the `p0-fixed` positions; no stale label after a failed draw (`lastMode` is advanced before the draw today) |
| UIX-16 | view | Normalise every wedge boundary through `normalizeAngle`; give the 60° MEDIA/legend boundary an explicit gap so the rounded cap clears the legend's clear space | ui/views/ModeSelectView.cpp | 1.5 | SCRN-13, UIX-04 | No inverted wedge; legend clear space preserved at dy=+150 with the corner measured against r=200 |
| SCRN-14 | controller | `ModeSelectController`: the 8 polling sites become `onInput`; software double-tap inside 1000ms; **D13** 3000ms back; entry stale suppression (250ms); 4 detents per step with a mid-step band offset; delete the interim 14px pin | controllers/screens/ModeSelectController.{h,cpp} | 4 | SCRN-04, SCRN-13, UIX-11, SCRN-25, INPT-17 | Double-tap selects, a single tap does nothing, a 3.5s hold backs out, the release that entered the screen does not self-select |
| UIX-17 | controller | Rim-annulus tap selects the wedge under the tap without entering it | controllers/screens/ModeSelectController.cpp | 1.5 | UIX-06, SCRN-14 | Pointing at a mode selects it; a disc tap does not |
| SCRN-15 | controller | Every `KnobMode` reaches a destination: exhaustive switch, no `default`; a CLIMATE placeholder view until `UIX-21` lands | controllers/screens/ModeSelectController.cpp, ui/views/PlaceholderView.{h,cpp} | 2 | SCRN-14 | Selecting CLIMATE renders something with a working back gesture (a 30s black-screen hang today) |

Model bindings: `NavigationModel::{selectedMode, activeScreen}`; `KnobModel::{detentCount, linkAlive}`; `MediaModel::availability[SPOTIFY]` **and** `availability[PC]` (the MEDIA wedge is unavailable only when both are); `ConnectivityModel::mqttConnected` for the CLIMATE wedge.

**WP4.2 MediaSpotify** (6 tasks, 26.0h)

| ID | Kind | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|---|
| CTRL-15 | view | `MediaView` base: the three-ring stack + disc rows, ported from `ensureUi`/`drawSetPoint`, render-only; `scalePx` no longer relies on caller discipline for its null check | ui/screens/MediaView.{h,cpp} | 5 | SCRN-03, UIX-04, UIX-05, MDL-14 | **Measured against the design, not the old layout** (SoundController builds its arc at rotation 180 with bg_angles 0–180, so pixel-identity is impossible): R1 420×420 arc_width 25 at rotation 120 with `bg_angles(0,300)`, indicator end on the r=198 centre-line, 33px cap, disc 264px, eyebrow/title/artist/state rows at dy −74/−20/+24/+70; no lock warning |
| UIX-18 | view | Spotify states: PLAYING, PAUSED, SYNCING (48px spinner), NO TOKEN, PREMIUM REQUIRED (read-only with a local 200ms spring-back), NO TRACK, OFFLINE, RATE-LIMITED, STALE, TITLE OVERFLOW; eyebrow + both R3 rail halves | ui/screens/MediaView.{h,cpp} | 4 | CTRL-15, UIX-08, UIX-10 | Each state reachable and legible, driven from its own `availability` slot; a dead target still lets a swipe reach PC; STALE never renders as live |
| MEDIA-13 | controller | `MediaScreenController`: `onEnter` requests state and pushes volume to the knob; `tick` ports the state machine **with the missing `WAIT_FOR_SYNC` timeout**; never names a target | controllers/screens/MediaScreenController.{h,cpp} | 4 | MEDIA-02, MEDIA-03, UIX-11, SCRN-04 | With no broker: renders, the knob works, a tap logs unavailable and does not wedge (tap-to-play is dead forever offline today). With a broker: a tap toggles the PC |
| MEDIA-14 | controller | `KnobBindingMachine` as a pure class (VOLUME / SEEK_IDLE / SEEK_ACCUM → actions), no ESP and no LVGL, taking a model snapshot rather than a reference | controllers/KnobBindingMachine.{h,cpp} | 6 | MEDIA-13, INPT-06 | Host: N detents produce one `SeekTo` and zero `SetVolume` while in SEEK; the terminating release is consumed |
| MEDIA-15 | test | Host tests for the binding machine + `FakeMediaTarget` + the PC protocol table | controllers/test/**, media_target/test/**, test/host/test_knob_binding_machine.cpp, test_pc_protocol.cpp | 4 | MEDIA-14, MEDIA-05 | Eight named cases green with no ESP header in the translation unit |
| MEDIA-28 | controller | Target selection sub-state, persisted last target, `targetLabel` population | controllers/screens/MediaScreenController.{h,cpp}, NVSManager.{h,cpp} | 3 | MEDIA-13, MEDIA-21, UIX-28, APP-13 | Pick Spotify, reboot, MEDIA reopens on Spotify; an unavailable target explains itself; overlay entry is the R3 rail tap from the `UIX-28` table |

Model bindings: `MediaModel::{activeTarget, volume, isPlaying, track.title/artist/id, positionMs, durationMs, positionSampledAtMs, knobBinding, availability[SPOTIFY], lastStateMs}`; `ConnectivityModel::internetAvailable`; `KnobModel::{detentCount, linkAlive}`.

**WP4.3 MediaPC** (1 task, 4.0h)

| ID | Kind | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|---|
| UIX-19 | view | Three mutually exclusive R2 forms — CONTINUOUS, TEN-PIP (ten 28° arcs with 2° gaps, created and destroyed with the state), ABSENT — plus the `targetLabel` eyebrow and the PC fault states | ui/screens/MediaView.{h,cpp} | 4 | CTRL-15, UIX-09, UIX-10 | Ten pips appear only in the digit-key fallback; an absent timeline never shows a cap; peak object count recorded against the 64KB LVGL heap |

Reuses `MEDIA-13/14/15` (controller), `MEDIA-04…10` (adapter — `seekStepPercent` selects the form) and `PCKNOB-22…28` (PC side). Model bindings: `MediaModel::{targetLabel, seekResolution, seekStepPercent, availability[PC], lastStateMs}`; `ConnectivityModel::mqttConnected`.

**WP4.4 MediaTargetSelect** (1 task, 3.0h)

| ID | Kind | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|---|
| UIX-20 | view | Overlay on the R1 diameter: two blocks (SPOTIFY 150–264, PC 276–420), candidate widened to 33px with its own cap, 48px candidate name, **per-target unavailable pip**, draining cancel ring on R3 | ui/screens/MediaTargetSelectView.{h,cpp} | 3 | UIX-04, UIX-05, UIX-12, UIX-28 | R3 still shows the unchanged current target underneath; both pips can be lit at once; entered by an R3 rail tap; commit permitted even when the candidate is unavailable |

Controller work is `MEDIA-28` + `UIX-12` (4000ms auto-cancel with the visible countdown, 3-detent candidate toggle, double-tap commit with toggle suppression, `knobBinding` forced to VOLUME on commit). Model bindings: `MediaModel::{activeTarget, availability[both], knobBinding}`; `KnobModel::detentCount`.

**WP4.5 Climate** — net new; the wheel's TEMPERATURE entry is dead today. (4 tasks, 14.0h)

| ID | Kind | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|---|
| UIX-21 | view | `ClimateView`: R1 target + permanent white cap, R2 indoor on the **same 0–40°C / 7.5°-per-degree scale**, R3 outdoor 4-segment rail, 48px value + a Montserrat 20 `°C` (**D9**), recoloured `IN 22°C / OUT 7°C` pair, delta / AT TARGET wedge, graduation pips at 10/20/30. **R3 never shares a cap or an indicator colour with R1/R2**, so the concentric scale break cannot read as one scale | ui/screens/ClimateView.{h,cpp} | 5 | UIX-04, UIX-05, SCRN-05, UIX-01, MDL-18 | The two arc ends coincide at setpoint; 0.5°C = 3.75° renders distinctly; no pip past the panel; R3 carries no cap and no shared colour |
| UIX-23 | view | Reuse the outdoor 4-segment rail verbatim (colours `#FF3A55/#FF7B39/#FFD85C/#39D9FF`, `(outdoor+20)/70`, the `segStart/segEnd` gap formula); record the per-index hot-to-cold colour quirk in both screens | ui/screens/ClimateView.cpp, ui/views/EnvironmentView.cpp | 2 | UIX-21, SCRN-17 | The same outdoor temperature looks identical on INFO and CLIMATE; the quirk is documented in both files |
| UIX-22 | controller | `ClimateScreenController`: 0.5°C per detent clamped 16–30, 400ms debounced publish, clamp flash, states NO SENSOR (120s stale) / NO OUTDOOR / NO HUMIDITY / MQTT DISCONNECTED / AT TARGET (±0.3°C) | controllers/screens/ClimateScreenController.{h,cpp} | 4 | SCRN-04, UIX-21, UIX-11, MDL-18 | One publish 400ms after the last detent; a stale indoor renders NO SENSOR with the target still settable; turning past a clamp flashes rather than swallowing detents |
| UIX-24 | controller | Rim-annulus tap sets `(angle−120)/7.5` snapped to 0.5°C — the one documented exception to the single-tap-target rule; **queue a pending publish across an MQTT outage, render it `#D7C06E` until the broker echoes, and flush it on the back gesture** | controllers/screens/ClimateScreenController.cpp | 3 | UIX-06, UIX-22 | Tap-to-value animates over 200ms ease-out; a turn during an outage publishes on reconnect and is never dropped; back publishes the debounced value instead of discarding it |

Model bindings (per **D4**): `ClimateModel::{targetTemp, humidity, humidityValid, pressure, indoorSampledAtMs, targetBand}`; `EnvironmentModel::{indoorTemp, outdoorTemp}` read-only; `ConnectivityModel::mqttConnected`; `KnobModel::{detentCount, linkAlive}`.

**WP4.6 MediaSeek** — a *state* of MediaSpotify and MediaPC, not a sixth screen: same shell, same disc, same back gesture; only the live ring and the readout change. (3 tasks, 9.0h)

| ID | Kind | Task | Files | Est h | Depends on | Verification |
|---|---|---|---|---|---|---|
| UIX-25 | view | R1 recedes (`LV_OPA_35`, cap off, `INDICATOR arc_width` still set), R2 to 21px (r_in 156, 4px clearance over R3's r_out 152), white cap + 19px grey ghost sharing the cached radius, time pair in the cap's colour | ui/screens/MediaView.{h,cpp} | 3 | CTRL-15, UIX-05, UIX-09 | Clearance measured at 4px; no `arc_width` animation anywhere; the ghost shares one cached radius with the cursor |
| UIX-26 | controller | Seek machine: 1% per detent, coarse ×3 on an 8-in-200ms burst, commit on release or 450ms idle, 1200ms confirm hold, overrun→`next()`/`previous()`, ten-step snap on the PC fallback, **TRACK_CHANGED discard via `track.id`**, 6000ms auto-revert, back discards | controllers/screens/MediaScreenController.cpp, controllers/KnobBindingMachine.cpp | 4 | MEDIA-14, UIX-10, UIX-15 | 40 detents produce one seek; a mid-scrub track change discards; back never commits; a lost confirm flashes `#FFAA00` rather than looking successful |
| UIX-27 | controller | Push per-screen haptic config (`snapPoint`, `detentStrength`) on `onEnter` so selectors feel like stops and magnitudes like fine detents | KnobController.{h,cpp}, model/KnobModel.{h,cpp} | 2 | PCKNOB-15, PCKNOB-19 | The knob console logs the pushed values; detent spacing visibly differs between ModeWheel and MEDIA |

Model bindings: `MediaModel::{knobBinding, pendingSeekPercent, positionMs, durationMs, positionSampledAtMs, seekResolution, seekStepPercent, track.id, isPlaying, volume, availability}`; `KnobModel::{detentCount, linkAlive}`.

**§4 roll-up:** WP4.0 13/32.5 + WP4.1 5/12.0 + WP4.2 6/26.0 + WP4.3 1/4.0 + WP4.4 1/3.0 + WP4.5 4/14.0 + WP4.6 3/9.0 = **33 tasks, 100.5h**, which with §3's Phase-3 packages (84 tasks, 283.0h) gives Phase 3 = 117 tasks, 383.5h.

---

## 5. Defect register

Every defect the eleven readers found, deduplicated, with the task that fixes it. Each row cites a `file:line` in the canonical tree.

| file:line | Defect | Fixed by |
|---|---|---|
| DisplayController.cpp:197 / CommandHandler.cpp:115 / KnobController.cpp:77 | One detent applied 1–3× (non-consuming flag, two tasks draining the same source) | INPT-21 |
| DisplayController.cpp:122 / KnobController.cpp:95,105 | `command_from_knob` `std::string` written and copied across tasks (use-after-free) | PCKNOB-09 |
| KnobController.cpp:88 | 512B UART ring read into a 256B stack array | PCKNOB-09 |
| KnobController.cpp:82,92 / FOC.ino:151,162 / serial.ino:78 | No framing: unterminated writes, concatenated tokens, no tail buffering — detents lost | PCKNOB-08, PCKNOB-09, PCKNOB-11 |
| CommandHandler.cpp:132,137 | `processCommand` splits on `:` only, so `setpoint=N` is `Unknown command` | INPT-22 |
| CommandHandler.cpp:297 (also :149) | Unguarded `std::stoi` reachable from MQTT — remote abort | APP-01 |
| DisplayController.cpp:273 / :254,261 | `CALIBRATE_ORIENTATION` reachable trap state; TEMPERATURE selection hangs 30s | APP-03 (Phase-1 floor gate), SCRN-15, DISP-17 |
| DisplayController.cpp:278 | LVGL pumped twice on INFO/DEVICE_INFO first paint and zero times elsewhere | DISP-13 |
| EnvironmentInfoScreen.cpp:376,435 / DeviceInfoScreen.cpp:324 | `lv_timer_handler()` re-entered under the lock mid-mutation | DISP-13 |
| DisplayController.cpp:421 | Two independent 30s display-power timers | DISP-15 |
| EnvironmentInfoScreen.cpp:338,377,441 / DeviceInfoScreen.cpp:244,325 | Views drive panel brightness, desyncing `displayIsOn` and overriding power save | SCRN-23 |
| LvglDisplay.cpp:139 | `init()` leaks DMA buffers and re-runs `lv_init()` on retry | DISP-21 |
| EspLcdSh8601DisplayFlushBackend.cpp:296 | `const_cast` + in-place byte swap of LVGL's draw buffer | DISP-22 |
| LvglDisplay.cpp:275 | The even-alignment rounder can push the flush area one pixel past the panel | DISP-23 |
| main.cpp:444 | Splash task never joined; two tasks can call `lv_timer_handler()` | DISP-24, APP-18 |
| DisplayController.cpp:96 | Touch SFX inverted; nothing fires on the actual press | INPT-13 |
| DisplayController.cpp:130 | Wake always lands on INFO and the wake tap leaks into that screen | INPT-29, INPT-11 |
| TouchPanel.cpp:546,609 | Latched flags cleared per call; one poll can latch two events | INPT-07, INPT-08 |
| TouchPanel.cpp:193 | `reset()` compiles out on C6 (`TP_RST_PIN -1`) — no hard reset ever happens | INPT-32 |
| EnvironmentInfoScreen.cpp:512,611 | Leave-animation freeze: `lv_anim_del` kills the shrink so `ready_cb` never fires; the outdoor shrink lacks `user_data` | SCRN-02, SCRN-19 |
| EnvironmentInfoScreen.cpp:397 | 64 unreachable lines behind `if (false && …)`, drifted from the live block | SCRN-21 |
| EnvironmentInfoScreen.cpp:81 | Date/time pulled every 16ms — ~6 heap allocations per frame | SCRN-22 |
| EnvironmentInfoScreen.cpp:500 | `positionTemperatureLabels()` invalidates two labels every frame | SCRN-18 |
| EnvironmentInfoScreen.cpp:844 / LightController.cpp:327 / SoundController.cpp:318 | Model-ish state written from the MQTT task with no synchronisation | MDL-03, MDL-31, MEDIA-06 |
| EnvironmentInfoScreen.cpp:46 | No sentinel: renders 18°C/0°C as if measured, and discards a true 18.0 | MDL-20 |
| ModeController.cpp:74,129 | `lastMode` advanced before a failed draw; `nextMode` draws with no `active` guard | SCRN-13, SCRN-14 |
| DeviceInfoScreen.cpp:93,108 | Any tap exits, contradicting its own hint; no first-frame stale guard | SCRN-25, INPT-08 |
| DeviceInfoScreen.cpp:351 | `scalePx` null-dereferences `display`; `lvglReady` latched before `buildUi` | SCRN-10 |
| DeviceInfoScreen.cpp:366 | Bars mapped 0–3 while `getSignalStrength()` returns 0–4 — the top bar is unreachable | MDL-22, SCRN-10 |
| InitializationScreen.cpp:101 | Boot status can only ever read `Starting \| MQTT pending` | SCRN-08 |
| SoundController.cpp:201 / MQTTManager.cpp:103 | The `sender=` prefix is never stripped for the callback — all inbound media state dropped | CTRL-02, NET-16 |
| SoundController.cpp:105 | `WAIT_FOR_SYNC` has no timeout: tap-to-play is dead offline forever | MEDIA-13 |
| SoundController.cpp:202,218 | Magic parse offsets; `atoi` on an empty payload silently mutes | MEDIA-05 |
| SoundController.cpp:165 | Up to 40 publishes/s, echoed back by the broker | MEDIA-07 |
| SoundController.cpp:330 / LightController.cpp:336 | `scalePx` relies on caller discipline for the null check | CTRL-15, CTRL-21 |
| LightController.cpp:244 | LIGHT has never published: the gate tests an NVS key with no writer | CTRL-03, APP-25 |
| LightController.cpp:184 | No publish rate limit — up to 300 msgs/s while dragging | CTRL-04 |
| LightController.cpp:255,259,328 | Brightness applied twice, published 0–255, accepted 0–100 — cannot round-trip | NET-22 |
| LightController.cpp:192 | `drawModeText()` takes the LVGL lock ~33×/s to write the same string | CTRL-21 |
| LightController.cpp:221,230 | Absolute-position hue/brightness drag; back gesture swallowed within 10ms of a drag | CTRL-22 |
| MQTTManager.cpp:14 / WiFiManager.cpp:15,32,50 | File-static instance dispatch ignores `handler_args` — blocks a second client | NET-19 |
| MQTTManager.cpp:53 | The callback mutates UI/screen state on the esp-mqtt task, including `esp_restart()` | NET-19 |
| MQTTManager.cpp:69,70 | The device subscribes to two topics it is the sole publisher of (echo loop) | NET-16, MEDIA-29 |
| MQTTManager.cpp:180 | `ESP_LOGD` prints the broker password in cleartext | NET-16 |
| MQTTManager.cpp:218 | `broker.address.uri` points at a destroyed function-local `std::string` | NET-16 |
| MQTTManager.cpp:241 | `skip_cert_common_name_check = true` unconditionally on every build | BLD-25 |
| MQTTManager.cpp:374 | One `sound_message` slot for every `esp32/sound/*` topic — messages overwrite | MEDIA-29 |
| main.cpp:517,519 | Unauthenticated remote `reset`/`clearNVS`/`otaUpdate`/credential writes over MQTT | APP-03, NET-23 |
| main.cpp:524 | `std::stof` on a possibly-prefixed payload; the light branch has an empty catch | NET-17 |
| main.cpp:533 | The light-brightness route is unreachable — no light topic is ever subscribed | NET-17, APP-15 |
| main.cpp:390,511 | Light publishes go to the sound broker and the light gate can never open | APP-25, NET-20 |
| main.cpp:186 | networkTask blocks 10–45s, starving MQTT reconnect and the pollers | NET-30 |
| main.cpp:400 | Unreachable null-check + realloc of the splash screen (would leak an LVGL tree) | APP-17 |
| main.cpp:427 | `presenceKnown` hardcoded false — DeviceInfo can never show a real battery state | PERI-05 |
| main.cpp:169 | `batteryHandler->analyze()` inside the 16ms render loop; `update()` unguarded | PERI-07, APP-19, PERI-19 |
| main.cpp:551 | commandTask literal priority 5 / 4096B preempts the renderer on a single core | APP-05 |
| TimeHandler.cpp:130 / WiFiManager.cpp:124 | Getters mutate `std::string` members written by another task | NET-10 |
| WiFiManager.cpp:456 | The "reconnected → re-check internet" branch is unreachable | NET-09 |
| InternetHandler.cpp:53 | Two `internetAvailable` owners with different refresh rules and consumers | NET-09 |
| WeatherHandler.h:28 / .cpp:12,74 | API key committed in a header and sent over plaintext HTTP; file-static response buffer; chunked ignored | PCKNOB-02b, NET-28, NET-29 |
| NVSManager.cpp:56,95 | `nvs_commit()` on every write (10 per `saveWiFiCredentials`) | APP-11 |
| NVSManager.cpp:82 | `readString` heap-allocates a raw `char[]` per read (~20 per boot) | APP-22 |
| NVSManager.cpp:339 | `readInt` leaves its out-param stale; one `port` variable reused across five reads | APP-22 |
| NVSManager.cpp:442 | The C shim returns a pointer into a file-static `std::string` the next call overwrites | APP-22 |
| CommandHandler.cpp:83 | Unbounded console accumulator (~6KB/s with no terminator) | APP-04 |
| CommandHandler.cpp:478 | `listNVSValues`, a read-only diagnostic, silently reloads all config from flash | APP-24 |
| SleepHandler.cpp:292 | Wake config reconfigures GPIO 1/2 — the knob UART pins — as plain inputs | PERI-23, INPT-23 |
| SleepHandler.cpp:360 | Light-sleep wake calls `resetActivityTime()` only: the display stays off and the CPU stays halved | PERI-22 |
| SleepHandler.cpp:203 | Deep sleep at 180s kills an in-flight OTA or recording | PERI-24 |
| SleepHandler.cpp:39 | Unsynchronised lazy singleton reachable from two tasks | PERI-26 |
| Speaker.cpp:720,721 | Per-call `std::vector` allocation in the audio path; odd length truncates and reports `ESP_FAIL` | PERI-13 |
| Speaker.cpp:640 | Full ES8311 bring-up (~40 I2C writes) per differently-formatted clip, on the touch bus | PERI-14 |
| Speaker.cpp:388,399 | `StartDacOnly` disables the codec ADC and nothing re-enables it — silent recordings | PERI-16 |
| Speaker.cpp:163 | File-static codec handle/flag never cleared by `deinitialize()` | PERI-26 |
| Microphone.cpp:118 | TX and RX I2S channels allocated independently on one port, no arbitration | PERI-15 |
| SDCard.cpp:139,230 | SD mounted on SPI2 alongside the panel; `INVALID_STATE` swallowed as "shared"; `spi_bus_free` on a bus it may not own | PERI-18 |
| SDCard.cpp:208 | Writes `/sdcard/esp32_test.txt` on every boot | PERI-17 |
| SoundPlayer.cpp:436 | `playReactionSound()` null-dereferences the optional `sdCard` | PERI-12, PERI-19 |
| BatteryHandler.cpp:344 | All-12-samples-failed returns the previous filtered voltage as a fresh reading | PERI-03 |
| BatteryHandler.cpp:531 | "Telemetry present" used as "battery present" | PERI-05 |
| OTAManager.cpp:123 | `lastError` written on the OTA task and read on commandTask, unsynchronised | PERI-27 |
| hal_config.h:297 | C6: `KNOB_TX=2`=`PWR_KEY`, `KNOB_RX=1`=`EXIO_INT`/`TOUCH_WAKEUP` | PCKNOB-03, INPT-23 |
| hal_config.h:290 | C6: IMU SDA/SCL/INT pins are the panel's D2/D3/MOSI/MISO | BLD-21 |
| main.cpp:375 | `KnobController` constructed with `-1` pins on the S3 AMOLED 1.75 | BLD-21 |
| sdkconfig.defaults.esp32c6:18 | Live WiFi SSID and password committed to git | PCKNOB-02b, BLD-20 |
| sdkconfig.defaults.esp32c6:28,29 | HTTPS-OTA allows plain HTTP in shipped images; `CONFIG_OTA_ALLOW_HTTP` undeclared | BLD-34, BLD-33 |
| sdkconfig.defaults.esp32c6:32 | Cert bundle enabled on C6 only — the plan's "no cert work" holds for 1 of 3 variants | BLD-16 |
| sdkconfig.defaults:15 | SPIRAM options meaningless on C6 and contradicted on S3 | BLD-33 |
| sdkconfig.defaults:48 / sdkconfig:2276 | `LV_USE_QRCODE=y` with zero call sites | UIX-02 |
| sdkconfig:2154 | Default font Montserrat 14 with zero first-party `text_font` calls | SCRN-05, UIX-03 |
| sdkconfig:2250-2252 | Light theme + GROW + 80ms implicit transitions under a true-black design | UIX-02 |
| sdkconfig:2055 | `LV_CIRCLE_CACHE_SIZE=4` against five distinct radii | UIX-05 |
| sdkconfig:1 / dependencies.lock:28 | Generated `sdkconfig` tracked at two paths (in-tree builds diverge); the lock target disagrees with it | BLD-01, BLD-04, BLD-37 |
| partitions.csv:3,5 | `encrypted` flags inert — the refresh token would sit in plaintext NVS | BLD-18 |
| controllers/CMakeLists.txt:5 | Includes `esp_timer.h` and `lvgl.h` without declaring them | BLD-28 |
| command_handler/CMakeLists.txt:5 | Every dependency declared public though the header only forward-declares | BLD-29 |
| display/CMakeLists.txt:9 | `PRIV_REQUIRES controllers ui sleep_handler` — the cycle edge | BLD-22 |
| main/idf_component.yml:2 | Duplicate lvgl pin (`^8` vs ui's `^8.3.11`) for a component that never includes it | BLD-27 |
| Kconfig.projbuild:3 | `CONFIG_QNOB_DEVICE_NAME` unreferenced and contradicts NVS's `ESP32Device` default | BLD-32 |
| SoundController.cpp:117 / LightController.cpp:229 | Back fires on any hold-release while the wheel demands 3000ms | INPT-08, SCRN-25 |
| EnvironmentInfoScreen.cpp:155,164 | Swipe-right is back on INFO but previous on media; any release also navigates | UIX-13 |
| PC sound_controller_app.py:137 | `send_command` undefined in the stale fork — every send raises | PCKNOB-01 |
| PC configure_tab.py:134,437,447 | The stale fork sends `configureSoundMQTTServer`/`listEEPROMValues`, which do not exist | PCKNOB-01 |
| PC mqtt_handler.py:22 / config_handler.py:43 | Live HiveMQ host, user and password hardcoded and committed | PCKNOB-02b, PCKNOB-02 |
| PC mqtt_handler.py:28 | Two sender identities in one app (`PC` vs `QNOB`); the prefix is matched as a substring | PCKNOB-35 |
| PC sound_controller_app.py:154,163 | `pause` maps to a blind `play_pause()` toggle and the UI then asserts "Paused" | PCKNOB-38 |
| PC sound_controller_app.py:171 | `forward`/`rewind` are track skip, and no firmware path even sends them | PCKNOB-22 |
| PC sound_controller_app.py:196 | The flat comma/colon state reply cannot carry a title | PCKNOB-21, PCKNOB-24 |
| PC sound_controller_app.py:78 | The Configure tab is addressed by hardcoded index 2 in five places | PCKNOB-33 |
| PC media_controller.py:92 | `- YouTube` treated as "playing" — true for any open tab | PCKNOB-25 |
| PC media_controller.py:39,66 | `refresh_interval`/`last_check_time` dead; `EnumWindows` runs every poll | PCKNOB-25 |
| PC windows_audio_controller.py:62 | `last_volume` raced between the monitor thread and Tk — echoes back to the device | PCKNOB-39 |
| PC tcp_subtab.py:29,99,102 | 254-host LAN sweep at construction; Tk read from a worker; loop variable captured by reference | PCKNOB-40 |
| PC serial_subtab.py:106 | Adopts `Device name: QNOB` (or a boot-log line) as the device name | PCKNOB-41 |
| PC config_handler.py:8 | `qnob_config.json` resolved against the CWD | PCKNOB-42 |
| FOC.ino:88 | `Serial.println(shaftAngle)` every control loop, blocking once TX fills | PCKNOB-07 |
| FOC.ino:96,99 | `setPoint` clamps 0–100 while the pending counters keep counting | PCKNOB-10 |
| serial.ino:67 | `delay(3)` per received character inside the FOC loop (~36ms per message) | PCKNOB-08 |
| serial.ino:7,12,16,22 | Four haptic setters unvalidated; `poles=0` NaN-poisons the PID | PCKNOB-19 |
| serial.ino:38 | `calibrate` forwarded under a name the screen does not register | PCKNOB-20 |

---

## 6. Dead code register

| Symbols | Location | Deleted by |
|---|---|---|
| `getMode()`, `transitionToMode()` (all-empty switch), `CALIBRATE_ORIENTATION`, SLEEP's empty default, 8 `register*` setters, 3 setpoint proxies | DisplayController.h:21,48,59,61-68 / .cpp:273,317,353-406 | DISP-17, DISP-19, INPT-22 |
| `DisplayBackendSelection`, `usesNativePanelBackend`, `panelBackendKind`, `DisplayBackendKind`, `getDisplayBackendSelection()` | DisplayBackendFactory.h:8-20 | DISP-18 |
| `DisplayFlushBackend::endWrite()` — both overrides empty, called twice per flush | DisplayFlushBackend.h / EspLcdSh8601…:179 / EspLcdGc9a01…:199 / LvglDisplay.cpp:241,261 | **DISP-28** |
| GC9A01 `ensureSwapBuffer()`, `swapBuffer*`, `txCommand()`, `setAddressWindow()` — unreachable on QSPI yet compiled | EspLcdGc9a01DisplayFlushBackend.cpp:286-380 | **DISP-28** (the SRCS-exclusion question is §9 Q35) |
| `setWiFiStatus()`, `setMQTTStatus()`, `previousProgress`, `wifiStrength` | InitializationScreen.h:13,16,41,42 | SCRN-08, SCRN-09 |
| `resetScreen()`, `lastActivityTime` | DeviceInfoScreen.h:47,99 | SCRN-11, SCRN-12 |
| `updateDateTime()`, `updateOutdoorTemperature()`, `isInactivityTimeoutReached()`, `INACTIVITY_TIMEOUT`=60000, `lastActivityTime`, `inactivityTimeoutReached`, `targetIndoor/OutdoorArcValue`, the `immediate` param + both branches, the `if(false&&…)` block | EnvironmentInfoScreen.h:57,59,68,88,89,104,136,146 / .cpp:397-460 | SCRN-20, SCRN-21 |
| `setMode()`, `isSoundModeActive()`, `isLightModeActive()`, `isTemperatureModeActive()`, `MODE_TEMPERATURE` as a destination | ModeController.h:12,53,56-58 | SCRN-16 |
| `setpoint`, `lastSetpoint`, `lastSentSetpoint`, `lastSentColor`, 5 unread timestamps, `lastWaterLevel`, `lastColorHue`, `modeChangeRequested`, `handleModeChange()`, `constrain<T>`, the GLOW/RAINBOW branches, `onBrightnessReceived` | LightController.h:26-48,67,73-76 | CTRL-13, CTRL-23, CTRL-24 |
| `lastDrawnSetpoint`, `colorChange`, `initialized`, `lastSentTime`, `lastSetpointChangeTime`, `updateMediaButtons()`, `handleMediaButtonEvents()` | SoundController.h:27,31,32,38,39,62,63 | CTRL-14, MEDIA-16 |
| `askSetpoint()`, both `sendCommand()` overloads (before `PCKNOB-15` revives one), `last_action_time`, the 7-function `knob_controller_*` C shim + its static string | KnobController.h:16,19,20,30,44-52 | PCKNOB-15, INPT-27 |
| `isHold()`, `isLongHold()`, `isReleased()`, `getGesture()`, `long_hold` (+10s threshold), duplicate `gesture`, `irq_pin`/`sda_pin`/`scl_pin`, 2 unreachable `first_*` branches, the per-poll gesture-register read, 3 unused `touch_gesture_t` values | TouchPanel.h:63-65,94-97,108,122,131,162-171 / .cpp:347,469,582,664 | INPT-32, INPT-33 |
| `hasSoundMessage()`, `getSoundMessage()`, `sound_message`, `processIncomingMessage()`, `unsubscribe()`, `getClientName()`, `hasSoundMQTTConfigured()`, the 6-function C shim, the `useLightConfig=true` branch, `MQTT_TOPIC_LIGHT_{CONTROL,BRIGHTNESS}` | MQTTManager.h:25,26,39,52,53,66,102-118 | NET-26, NET-15, NET-20 |
| `scanNetworks()`, `getScanResultCount()`, `scan_result_count`, `WIFI_SCAN_DONE_BIT`, `on_connected`/`on_disconnected` + setters, `startAPSTAMode()`, `getSSID()`, `disconnect()`, the 6-function C shim | WiFiManager.h:31,59-63,80,91,92,108-124 | NET-27 |
| `lastTimeCheck`, `TIME_SYNC_INTERVAL`, `getTimeHandler()`, `getWeatherHandler()`, `getWeatherTemperature()`; 7 TimeHandler date/time accessors; `getHumidity()`, `getWindSpeed()`, `getCondition()`, `setLocation()`, `setApiKey()` | InternetHandler.h:18,23,36,37 / TimeHandler.cpp:151-184 / WeatherHandler.h:42-44 | NET-24, NET-26, NET-29 |
| C shim `nvs_manager_*`, `saveSoundTCPServer()`, `saveLightTCPServer()`, 4 TCP members + 4 keys (plus a one-boot erase of the orphaned flash entries), `getConfigurationInfo()`, `getStaticIPInfo()` | NVSManager.h:27-30,70-73,148-159 | APP-22 |
| `cmdShowNetworks`, `cmdCommInfo` (log-only stubs), `Command::name`, the 3 duplicated readouts, `commands["+"]/["-"]/["setpoint"]/["calibrateOrientation"]` and their handlers | CommandHandler.cpp:175-184,203-211,279-287,296-300,414-417,467-510 | APP-23, INPT-21, INPT-22, APP-14 |
| `displayTaskHandle`, `networkTaskHandle`, `commandTaskHandle`, `startupSplashTaskHandle` (assigned, never read) | main.cpp:111-114 | APP-20 |
| `isRecording()`, `getLastRecordingPath()`, the two public effect-filename statics | MediaController.h:26,27,29,30 | PERI-12 |
| `playReactionSound()`, `recordToFile()`, `hasSdCard()`, `getSoftwareVolume()`, mic gain setters + the dead gain loop, `SoundRecorder::isRecording()`, `kStartupSound` (317 lines), 3 unreachable effect fallbacks, `SDCard::{readFile,writeFile,writeTextFile}`, `logDirectoryTree()` | media/* | PERI-12, PERI-17 |
| `isCharging()`, `currentMilliamps`, `lastRawSpread`, the non-consecutive branch of `pmuReadRegisterH5L8` | BatteryHandler.h:29,47,81 | PERI-08, MDL-24 |
| `mutex_ptr`, the 8-function `sleep_handler_*` C API, 5 timeout setters (two writing the same field), `isCpuFreqReduced()`, `isEnabled()`, `WAKEUP_PIN_KNOB` | SleepHandler.h:19,49,53,56-60,86,116-125 | PERI-25 |
| `setServerCert()`, `setAllowInsecure()` + both members | OTAManager.h:19,20,34 | PERI-27 |
| 9 dead Kconfig symbols (`QNOB_ENABLE_{TOUCH,IMU,KNOB_UART,DEEP_SLEEP}`, `QNOB_KNOB_BAUD_RATE`, `QNOB_INACTIVITY_TIMEOUT_SEC`, `QNOB_WIFI_CONNECT_TIMEOUT_SEC`, `QNOB_DEVICE_NAME`, `QNOB_LOG_LEVEL`) | board_hal/Kconfig:89-133, Kconfig.projbuild:3-38 | BLD-32, BLD-21 |
| `LV_USE_QRCODE`, SPIRAM opts, `SOC_WIFI_HE_SUPPORT`, `ENABLE_WIFI_STATION`, **`OPENTHREAD_ENABLED`**, `OTA_ALLOW_HTTP`; the unused 1.64MB spiffs + `esp_secure_cert` + `fctry` partitions; `sdkconfig.old`; `NUL`; the stale `build/` | sdkconfig.defaults*, partitions.csv | UIX-02, BLD-33, BLD-01, BLD-02, **§9 Q26** |
| `last_setPoint`; the `zeroPos` and `sp?` handlers | HapticController3.ino:15, serial.ino:70-80 | PCKNOB-05, PCKNOB-20 |
| `PC/src/media.controller.py` (an unimportable byte-identical duplicate), `populate_test_data()`, `update_player_info()`, `get_current_player()` (no caller), `SENDER_ID`/`client_id`, `misc_frame`, `toggle_autoscroll()`, `toggle_timestamps()` | PC/src/* | PCKNOB-37, PCKNOB-24 |

---

## 7. Critical path

Derived edge by edge from the `Depends on` columns as the longest path over (id, hours, dependsOn), not hand-assembled — which is how the previous draft acquired a reversed edge and three edges that existed nowhere in the tables. **Every edge below appears in a `Depends on` cell in §3.** `BLD-39` recomputes it.

| # | Task | h | Cumulative | Edge taken from |
|---|---|---|---|---|
| 1 | PCKNOB-02b | 1.0 | 1.0 | — |
| 2 | BLD-20 | 1.0 | 2.0 | BLD-20 deps `PCKNOB-02b` |
| 3 | BLD-03 | 3.5 | 5.5 | BLD-03 deps `BLD-02, BLD-20` — the BLD-20 chain (2.0h) is the longer predecessor |
| 4 | MDL-01 | 2.0 | 7.5 | MDL-01 deps `BLD-03` |
| 5 | BLD-05 | 2.0 | 9.5 | BLD-05 deps `MDL-01` |
| 6 | BLD-24 | 2.0 | 11.5 | BLD-24 deps `BLD-05` |
| 7 | BLD-41 | 2.0 | 13.5 | BLD-41 deps `BLD-05, BLD-24` |
| 8 | BLD-11 | 1.5 | 15.0 | BLD-11 deps `INPT-06, BLD-41` |
| 9 | INPT-07 | 6.0 | 21.0 | INPT-07 deps `INPT-06, BLD-11` |
| 10 | INPT-08 | 6.0 | 27.0 | INPT-08 deps `INPT-07` |
| 11 | INPT-11 | 4.0 | 31.0 | INPT-11 deps `INPT-08` |
| 12 | INPT-12 | 3.0 | 34.0 | INPT-12 deps `INPT-08, INPT-11` |
| 13 | INPT-13 | 3.0 | 37.0 | INPT-13 deps `INPT-12, PERI-10` |
| 14 | INPT-14 | 4.0 | 41.0 | INPT-14 deps `INPT-12, INPT-13` |
| 15 | SCRN-08 | 5.0 | 46.0 | SCRN-08 deps `SCRN-04, SCRN-07, INPT-14` |
| 16 | DISP-11 | 7.0 | 53.0 | DISP-11 deps `SCRN-04, SCRN-08, SCRN-03, DISP-08` |
| 17 | APP-14 | 4.0 | 57.0 | APP-14 deps `DISP-11, MDL-03` |
| 18 | APP-16 | 5.0 | 62.0 | APP-16 deps `APP-06, APP-14, APP-15` |
| 19 | APP-27 | 4.0 | 66.0 | APP-27 deps `APP-16` |
| 20 | APP-17 | 6.0 | 72.0 | APP-17 deps `APP-27` |
| 21 | BLD-26 | 2.5 | 74.5 | BLD-26 deps `APP-17, BLD-24` |
| 22 | BLD-28 | 6.0 | 80.5 | BLD-28 deps `BLD-22, BLD-24, BLD-26, BLD-44, INPT-21, PERI-11` |
| 23 | BLD-30 | 3.0 | 83.5 | BLD-30 deps `BLD-28, BLD-45, BLD-03` |
| 24 | BLD-36 | 3.0 | **86.5** | BLD-36 deps `BLD-09, BLD-30, BLD-39` |

**Total on the path: 86.5h of 960.0h — 9.0%, over 24 of 320 tasks.** The work is wide, not deep: 91% of it sits off the path and parallelises once the model, the queue and the Phase-1 component edges exist. The path is made of the measurement root, the CMake edge declarations, the input router, the boot controller, the router, the command router, the composition split and the CMake tail — not of any screen. `DISP-11` (7h) and `BLD-28`/`INPT-07`/`INPT-08`/`APP-17` (6h each) are the longest single tasks on it.

Note what is **not** on the path and why the previous draft thought it was: `MEDIA-16` depends on `DISP-11`-free predecessors only, so the old `MEDIA-16 → DISP-11` edge was reversed; `INPT-06` has no predecessor, so `MDL-31 → INPT-06` never existed; `MDL-30` waits on every struct including `MDL-28` (6h) and `UIX-10` (5h), which the old path skipped.

Chains that run close behind, each with its cumulative reach from the same root (the figure that decides whether it becomes critical, not its own length):

- **Display teardown** — `DISP-11` → `DISP-12` → `DISP-19` → `BLD-22` → `BLD-23`, reach **66.5h**. Branches off the path at `DISP-11` and has 19.5h of slack against `APP-14`'s successor chain. `DISP-11` is the single largest behaviour-carrying move in the tree.
- **Peripherals / sleep** — `DISP-14` → `DISP-15` → `PERI-22` → `PERI-23` → `PERI-25`, reach **64.0h**.
- **Knob** — `INPT-14` → `INPT-21` → `INPT-22` → `PCKNOB-15` → `PCKNOB-17` → `PCKNOB-18`, reach **63.5h**, behind a hardware-gated prefix `PCKNOB-03` → `INPT-23` (7h). If §9 Q1 answers badly this chain stalls entirely and `UIX-25`…`UIX-27`, `INPT-25`, `PCKNOB-16`, `PCKNOB-19/20` and `PERI-23` stall with it.
- **Spotify** — `MDL-04` → `MDL-14` → `MEDIA-02` → `BLD-12` → `MEDIA-04` → `MEDIA-05` → `MEDIA-06` → `MEDIA-22` → `MEDIA-23` → `MEDIA-27`, reach **61.5h**. Additionally gated on `BLD-16`/`BLD-17` (TLS config) and on `PCKNOB-32`/`APP-12` for provisioning. Becomes critical if the §9 Q3 heap answer forces a redesign.
- **ModeWheel** — `INPT-17` → `SCRN-14` → `SCRN-15` → `SCRN-16`, reach **54.5h**; `SCRN-14` cannot start before the 8 polling sites are cut over.
- **Climate** — `MDL-18` → `UIX-21` → `UIX-22` → `UIX-24`, reach **42.5h**; its real gate is `UIX-11` (the knob accumulator, reach 34.0h), not the model.

`BLD-06` (host harness, 4h, reach 14.0h) is not on the path but gates 21 test tasks; build it first regardless. `BLD-39` is not on the path either, but nothing downstream of it can be trusted until it has run once.

---

## 8. Test strategy

### 8.1 Host tests — the only layer testable without flashing

A plain CMake project at `esp-idf/test/host`, one vendored single-header runner, device flags matched (`-std=gnu++2b -fexceptions -fno-rtti`) plus ASan/UBSan and `-Werror`. `FakeClock` for all time-dependent policy, `FakeTransport` for the pollers, `FakeMediaTarget` for the binding machine. `BLD-10` establishes a glob-or-list registration convention so a suite added by any task runs without a CMake edit. `NoHeapScope` wraps **every** notify path in **every** model suite — that is what makes "no heap in the render path" an assertion rather than a promise. `check_model_purity.sh` fails the build if anything under `components/model` gains an ESP, FreeRTOS, driver, LVGL or NVS include. `run-host-tests.{sh,ps1}` runs it all and `verify-all.{sh,ps1}` rolls it up.

| Suite | Owning task | Covers |
|---|---|---|
| `test_observable` | MDL-04, MDL-06 | capacity, over-capacity refusal, notify-on-change, 0 allocations / 10k sets |
| `test_mutation_queue` | **MDL-40** | FIFO under capacity; coalesce-in-place; at capacity+1 the **D18** policy (coalesce-latest overwrites, never-drop retries then faults); overflow counter; drain-task assertion |
| `test_model_snapshot` | **MDL-41** | seqlock consistency: two readers against one writer, 100k iterations, every snapshot from one generation |
| `test_bands` | MDL-08 | all eight thresholds, `bandProgress` against the old lerp bit-for-bit, `-1.0f`, NaN |
| `test_domain_helpers` | MDL-11 | `ColorMath` against a 1/360 hue reference table and both call sites' value args; `DateFormat` well-formed and every malformed shape |
| `test_ratelimit` | MDL-13 | interval boundaries, `flush()`, a 100-change burst collapsing to one fire |
| `test_media_model` | MDL-15 | clamping, out-of-range rejection (an empty payload must not mute), echo suppression, truncation, **the two `availability` slots independent** |
| `test_media_amendments` | **UIX-10** | `pendingSeekPercent` −1 vs 0, `positionSampledAtMs` validity, `seekResolution`/`seekStepPercent` gating, the `lastStateMs` staleness boundary, `track.id` change detection, the `indoorSampledAtMs` 120s gate |
| `test_light_model` | MDL-17 | clamps at 0/100, hue at the `i%6` boundary, the `LedMode` cycle's first-ever execution, one publish per window |
| `test_climate_model` | MDL-19 | validity gating, NaN/∞ rejection, target clamp 16–30, band tracking |
| `test_environment_model` | MDL-21 | 0.5°C hysteresis (0.4 suppressed, 0.6 admitted), `FixedString` truncation, formatted-date regeneration, band continuity, the sentinel |
| `test_connectivity_model` | MDL-23 | four flags independent, strength 0/4/5, five distinct bar values |
| `test_power_rules` | MDL-25 | 13 curve knees + 3 midpoints, clamps above 4.20V and below 3.30V, `-1.0f` sentinels, presence ≠ connected |
| `test_navigation_model` | MDL-27 | `request_consumed_once` (fails against a flag implementation), one activity clock, no transition to a dropped screen, `initProgressPct` |
| `test_knob_model` | MDL-29 | **the most valuable suite in the tree** — net-zero deltas, absolute-never-stored, `mode_switch_does_not_leak_setpoint`, clamp at both ends, link timeout, per-divisor accumulator resets |
| `test_pollers` | **NET-31** | push-on-change vs steady state, RSSI→`SignalBars`, minute-granularity time filtering, battery sentinel pass-through, all against `FakeTransport` with no `esp_wifi` |
| `test_knob_binding_machine` | MEDIA-15 | the eight named cases: SEEK entry gated on caps, accumulate-then-one-seek, idle flush at exactly 450ms, release flush, auto-revert, clamp at 0/100, unknown duration blocks SEEK, a VOLUME detent reaches the target immediately |
| `test_pc_protocol` | MEDIA-15 | the real `NET-01` captured payloads, comma-and-colon titles, both setpoint separators, out-of-range and truncated input |
| `test_state_parser` / `test_error_map` | MEDIA-27 | 8 checked-in Spotify fixtures (playing, paused, 204, podcast with null artists, 401, 403 premium, 429 with `Retry-After`, truncated), zero-allocation parse, the status→error table |
| `test_command_parsers` | APP-21 | one case per `configureMQTTServer` arity, the `password == "-"` reuse rule, `connectWifi` with and without a slot, `setpoint:abc` rejection |
| `test_boot_ramp` | SCRN-08 | the splash ramp on `FakeClock`: 0→100 over 2500ms, a request at 2900ms, no backward jump |
| PC-side pytest | PCKNOB-06, PCKNOB-32, PCKNOB-27 | `pc_media_protocol` verbs and the v2 reply; `spotify_auth` (challenge = S256(verifier), loopback-only bind, state mismatch refused); `browser_bridge` (tokenless refused, not LAN-bound) |

### 8.2 On-device gates, per phase

Each phase ends buildable and flashable; do not batch. `BLD-39` guarantees no task depends on a later phase, so every gate below is reachable with only that phase's own work plus its predecessors.

- **P0** — Three variants build from a clean tree. Boot to INFO. `SCRN-01`, `INPT-01`, `NET-01`, `APP-02`, `DISP-01` captured and transcribed; three `graph-baseline-*.json` archived; `BLD-38`'s threshold table signed (§9 Q11); `BLD-39` exits 0. **Nothing about device behaviour has changed.** Tag `p0-baseline`.
- **P1a** (WP1.0) — `setpoint:abc` over serial *and* MQTT logs a rejection instead of rebooting. `connectWifi` and `calibrateOrientation` over plaintext MQTT are refused **and the UI stays responsive**. 2KB with no newline does not exhaust heap. Commands still land <100ms with the renderer un-preempted. PC play/pause and volume round-trip (dead before). A LIGHT drag produces `ledRing/#` traffic at ≤40 msg/s with a correct final value (dead before). Every temperature string reads `°C`; no light-theme rectangle; no pressed-object scaling. `SCRN-01` and `INPT-01` re-captured; the four repaired behaviours recorded as intended deltas. Tag `p0-fixed`.
- **P1b** (WP1.1–WP1.4) — **No visible regression against `p0-fixed`**, with these intentional deltas allow-listed: `PERI-05` (DeviceInfo battery wording), `PERI-07`/`PERI-08` (battery cadence and dropped fields), `NET-09` (single internet owner), `NET-28`/`NET-29` (weather over HTTPS from config), `NET-30` (network-task cadence), `NET-07` (timezone source), `PERI-03` (an all-fail battery read now reports invalid), `MDL-07` (the WiFi label can show four bars), `APP-21` (`parseInt` rejects leading whitespace; `URL:mqtts://host:user:pass` parses), `PCKNOB-06` (PC setpoints outside 0..100 are rejected, not clamped). `ModelTrace` values track every screen as the knob turns, temperatures publish and the AP is pulled. **`MDL-32`'s off-displayTask write counter reads exactly 0.** Free heap and `size-components` within the `BLD-38` rows. Host suite green; purity guard green. **Since 2026-09-13 the comparison target is the ported main screen set** (bench 1: fonts, EnvironmentInfoScreen with intro/outro, DeviceInfoScreen pages, placeholder matrix pages, rotation 0 with touch mirror X/Y on the 1.75), so the pre-port screens no longer count as the reference; the P1b screenshots are taken on that set.
- **P2** — Full gesture matrix on every screen against the `p0-fixed` checklist. **One detent = exactly one step**, repeatably (`INPT-21`/`INPT-22` are in this phase, so the gate is deliverable). All six SFX with the click at finger-down and the release at finger-up. A wake tap does not navigate. A 1s hold no longer exits MEDIA or LIGHT; 3s does. `grep touchPanel->` clean. Router ring high-water below capacity with overflow 0. **A knob still on old firmware works** (legacy bare `+`/`-` accepted, **D6**).
- **P3** — Per screen after its split: enter, every gesture, knob, back, re-enter with a freshly synced value; **and its wedge legend measured against the 188px / 12-character budget**. The boot arc animates through SD and WiFi init. The INFO leave animation completes 20/20 under MQTT load. MEDIA on both targets including every fault state driven from its own `availability` slot. LIGHT drag then back 10/10, matching the `p0-fixed` layout (**D19**). CLIMATE knob, rim tap, stale-sensor, offline-queue and AT TARGET states. SEEK: 40 detents → one seek; a mid-scrub track change discards; back discards; a lost confirm flashes. Spotify: volume, play/pause, seek, a rotated refresh token surviving a power cycle, a Free account latching read-only. The 50-cycle lifecycle soak with flat largest-free-block (`SCRN-28`/`SCRN-24`).
- **P4** — The whole navigation graph twice, one `Changing screen` per hop, one `onEnter`/`onExit` each. ≈33 LVGL pumps/s on every screen. No banded repaint. One display-off at the timeout; four sleep stages off one clock; wake restores the **previous** screen and the CPU; detents survive a light-sleep wake. `calibrateOrientation` leaves the UI responsive. Colour fidelity on both panel backends; both still build after `DISP-28`. The `BLD-45` dump shows the `display` cycle edge gone.
- **P5** — Every topic in the table round-trips per topic, including `esp32/light/brightness` (dead today) and a setpoint plus a state reply arriving back to back. A malformed payload logs a rejection. `clearNVS` over plaintext MQTT refused. Every model write on DisplayTask. A knob sweep bounded to single-digit msgs/s. No `s_mqtt_manager`/`s_wifi_manager` remaining. No broker password at any log level.
- **P6** — Boot log identical to `p0-fixed` on C6 with and without an SD card (`[S3-1.75]` if the board is on the bench). `main.cpp` under 100 lines. Ten soft resets render the splash. The splash task is joined before router init. Boot heap within the `BLD-38` row.
- **P7** — `fullclean` build on all three variants. `check-component-graph.sh` exits 1 against each of the three archived graph baselines and 0 against the current graph. A cold boot with a finger on the glass causes no navigation. Idle I2C near zero if `INPT-34` ships. `verify-all.sh` green, including the §1/§7 roll-up check.

### 8.3 Phase 0 baseline measurements

Items 1–6 are captured on the pristine tree in WP0.2, before any edit. `p0-baseline` marks the measured tree; **`p0-fixed` (`BLD-40`, Phase 1) is the reference every later phase diffs against**, because the theme flip and the four repairs invalidate the first set of screenshots and the first gesture checklist.

1. `idf.py size` and `size-components` per variant with the `display`, `ui`, `lvgl` and `controllers` rows called out, **plus `build/project_description.json` archived per variant as `dev_notes/graph-baseline-<variant>.json`** — the artifact `BLD-30` needs six phases later, captured before `BLD-02` deletes the stale in-tree build (`BLD-03`).
2. `esp_get_free_heap_size()` and `heap_caps_get_free_size/largest_free_block(MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL)` at HAL init, after `LvglDisplay::init()`, after tasks start, with MQTT-TLS up, and on the SOUND screen (`APP-02`).
3. The `LvglDisplay` init line verbatim: buffer lines allocated, backend minimum and preferred, even-alignment, rotation (`DISP-01`).
4. One screenshot per screen, one boot log, one full traversal log including the `Changing mode` and `ENV_ARC_V5` lines, and the measured first-INFO-paint duration (`SCRN-01`; re-taken under the dark theme by `BLD-40`).
5. The 8-gesture / 21-site / 6-SFX input checklist as observed, including the fast-spin negative case (`INPT-01`; re-run by `BLD-40`).
6. The live MQTT contract: every topic, direction, payload grammar and the `sender=` envelope, cross-checked against `MQTTManager.h` and the four literal-topic sites (`NET-01`; the light publish rate is re-recorded by `CTRL-04`).
7. The model RAM budget, including **D18**'s 1.28KB queue and every subscriber array, plus the exact command that measures it (`MDL-01`).
8. **The acceptance-threshold table** — max binary growth per phase in absolute bytes against `ota_0` = 3,276,800, max internal-DMA free-heap loss, max largest-free-block loss, required draw-buffer line count (`BLD-38`). Every "within baseline" / "binary shrinks" verification in §3 and §4 resolves against this table; without it no measurement can reject anything, and the three ad-hoc bars the previous draft invented ("within 2KB", "within a few hundred bytes", "within baseline") are replaced by named rows.

---

## 9. Open questions for the owner

Deduplicated across all eleven clusters and the three design judges. Q1–Q4 block scheduled work.

1. **Is the ScreenBase_V2 J16 header physically wired to GPIO 1 and GPIO 2 on the built boards?** If yes, no software change reconciles the knob UART with PWR_KEY and the TCA9554 touch-wake line, and one of the three features must be dropped on this revision. Blocks `PCKNOB-03`, `INPT-23`, `PCKNOB-15…20`, `INPT-25`, `UIX-27`, `PERI-23`.
2. **Is the SD card genuinely on the panel's own QSPI data/clock lines (IO4/IO5/IO11), or is `hal_config.h` wrong?** Decides whether `PERI-18` is arbitration, a no-SD-while-active policy, or a schematic correction. Everything SD-backed depends on it.
3. **Does the two-TLS-session heap measurement pass?** If `MEDIA-26` comes back outside the `BLD-38` threshold, Spotify becomes mutually exclusive with MQTT at runtime rather than concurrent — a product decision, not an engineering one.
4. **May the knob firmware change?** LF framing and `+N`/`-N` (`PCKNOB-08`, `PCKNOB-10`) require reflashing every knob. **D6** sequences the tolerant screen-side parser first so no window loses detents, but a knob flashed alone still needs the screen updated before its counts are read.
5. **Does the `sender=<name>,` envelope stay, and where is it stripped?** Centrally in `MQTTManager` before the callback (one line, changes what every subscriber sees) or per adapter (safer, repeated)? It is also the only self-echo guard, and its default device name is `QNOB`.
6. **Should the state reply become JSON or a delimited form with escaping?** A YouTube title containing a comma or a colon corrupts today's parser. JSON costs a parser and heap on a 512KB part; escaping costs a hand-rolled codec on both ends. `PCKNOB-21` must state the choice explicitly.
7. **One MQTT broker or two?** NVS carries a complete second (light) credential set with no writer and no configuration command, and only one client is ever constructed. Deleting the profile flips LIGHT from never-publishing to always-publishing on a shared broker. `NET-20` is the transport decision, `APP-25` the provisioning consequence.
8. **Do the `ledRing/*` topic names stay,** or move to the declared-but-unused `esp32/light/*`? Only the external LED device knows, and it also decides the 0–100 vs 0–255 brightness contract.
9. **Should `esp32/command` remain a full remote command channel?** `APP-03` already refuses secret, destructive and UI-freezing commands over plaintext; `NET-23` decides the permanent allowlist.
10. **Plaintext NVS for the Spotify refresh token, or flash+NVS encryption?** Encryption in release mode is irreversible on real hardware and breaks plain flashing and cross-device OTA images. And how often may the device rewrite flash for rotation (~8,760 writes/year)?
11. **Are the `BLD-38` thresholds right?** The task produces the table; the owner signs the numbers. Without a signature the measurements still cannot reject anything.
12. **Does waking restore the user's screen, or keep today's always-INFO?** **D8** assumes restore. And what is the single inactivity timeout (30s / 30s / dead 60s today), with INFO possibly holding longer than the control screens?
13. **Should a press make a sound?** Today the press sample plays on tap-release and the release sample only on holds under 3s, with nothing on the actual press. And is touch SFX global or per-screen suppressible?
14. **Is the `UIX-28` gesture table acceptable** — swipe-left/right = next/previous, an R3 rail tap opens the target overlay, double-tap commits inside the overlay with its play/pause toggles suppressed? And does a plain tap still exit DEVICE_INFO and jump from INFO to the wheel? Muscle-memory changes on a device whose only input is a small round surface.
15. **What does INFO show before any data arrives** — a visible no-data state, or today's confident hardcoded 18°C/0°C? This decides whether every reading needs a validity bit (`MDL-20` assumes yes).
16. **Do the mode-wheel icons stay as text placeholders,** become the compiled glyphs of `UIX-14`, or wait for SD-backed images (which would need `LV_USE_FS_FATFS`, currently off)?
17. **Is the LED mode feature (Static/Glow/Rainbow) wanted?** Fully implemented, completely unreachable, and the LIGHT screen's gesture budget is already full. `CTRL-23` implements either answer.
18. **Does the LED ring firmware apply the brightness channel it receives?** Decides whether `NET-22` makes the ring correct or twice as bright.
19. **Should recording be supported on C6 at all?** Making it work needs the ES8311 ADC path written from the datasheet (`PERI-16`); the alternative is declaring capture S3-only.
20. **Is the browser extension distributed or developer-side-loaded?** If side-loaded only, the 10%-granularity digit-key path is the shipped behaviour for everyone else — and it steals window focus.
21. **Where does the Spotify client ID come from** — per-user registration, or one ID shipped in the PC binary (which must then appear in the binary and have its redirect URI registered)?
22. **What happens on a Spotify Free account?** Hide the target, show it disabled with an explanation, or let control fail visibly while read-only state still renders? The design chose the third (PREMIUM REQUIRED / READ ONLY); confirm it.
23. **Are `showNetworks`, `commInfo`, `+`/`-` over serial, and `setpoint:N` over MQTT features to preserve?** All four are on the deletion list; the PC app or a Home Assistant automation may depend on them.
24. **Does the PC app belong inside the firmware tree** (`esp-idf/PC`, versioning together) or at a top level? And should `CommandHandler` answer every command over MQTT, not just the four that do today?
25. **Should `components/controllers` keep its name** once its contents are entirely new (`ScreenController` base, per-screen controllers, `ScreenRouter`), or split into `controllers` + `screen_router`? And does `DisplayPowerController` stay a class or fold into the panel backend?
26. **Reclaim the unused 1.64MB spiffs partition and the esp-matter `esp_secure_cert`/`fctry` entries?** Reclaiming means an incompatible partition table and a full erase on every existing device.
27. **Must the host test build work on native Windows,** or is WSL-only acceptable (what the build scripts already assume)? And does anything run `verify-all.sh` automatically, or is it discipline?
28. **Does the INFO clock need DST correctness, and where does the timezone live?** `NET-07` moves it out of the literal `3` and the hardcoded `pool.ntp.org`/`time.nist.gov` into Kconfig/NVS and records a decision; whether DST is handled at all is a product call.
29. **Is the Spotify access token cached in NVS or held only in RAM?** `APP-10` adds `spf_expiry` but no access-token key. Caching survives a reboot without a round trip but writes a bearer token to flash.
30. **Erase the orphaned `snd_tcp_*`/`lgt_tcp_*` NVS keys on already-provisioned devices, or leave them?** `APP-22` implements the one-boot erase; whether to touch a shipped device's NVS is the owner's call.
31. **Is the S3 AMOLED 1.75 still a shipping target?** Roughly fifteen verifications say "three variants build", six need the physical board, and it has no knob at all (`KNOB_TX/RX = -1`), which makes `KnobModel`'s replicated state and the `onEnter` push inapplicable there.
32. **Rename `NUM_POLES` / the `poles=` verb to `detentsPerRev`?** It is detents per revolution, not motor poles (the motor is `BLDCMotor(8)`), and `MDL-28` propagates the misnomer into `KnobModel`'s haptic config. Any existing tooling that sends `poles=` breaks on a rename.
33. **Does the LVGL pump stay on displayTask, or move to a dedicated lvglTask?** `DISP-13` assumes it stays, which is the safest reading of the single-core constraint; a dedicated task changes the lock discipline everywhere.
34. **Does an in-flight OTA get a visible surface?** `PERI-24` lets it hold the device awake for minutes and `OTAManager` already tracks progress, but no view shows it, so the user sees an unresponsive-looking device.
35. **Exclude `EspLcdGc9a01DisplayFlushBackend.cpp` from SRCS on QSPI boards,** or keep compiling it as an unreachable stub? It costs C6 flash and hides breakage until someone builds the SPI board. `DISP-28` deletes the dead members either way.
36. **Is git history rewritten for the three leaked credentials, or are the old values accepted as burned?** `PCKNOB-02b` rotates all three at their providers regardless; the history decision affects whether this repository can ever be published, and whether the old HiveMQ account, PSK and API key must additionally be watched for use.
37. **Which of the four boards are actually on the bench** (C6-AMOLED-1.43, S3-AMOLED-1.75, S3-LCD-1.28, V11 knob)? Every `[S3-1.75]`, `[S3-1.28]`, `[AXP2101]` and `[ES8311-capture]` verification is deferred without them, and the deferral must be recorded rather than silently skipped.
38. **Sign off the D18 never-drop list.** Which model fields may be coalesced under burst and which must never be lost is partly a product judgement — a dropped RSSI sample costs nothing, a dropped play-state transition leaves the UI permanently wrong.
39. **Does LIGHT get an Instrument-shell redesign?** **D19** exempts it, because the winning design specifies no Light screen and `LightView` cannot be held to both "matches baseline" and "uses the ring shell". If a redesign is wanted it is a new design task plus a rewrite of `CTRL-21`/`CTRL-22`.

## 10. Known residuals from the resolution check (all minor)

The independent check that passed sections 1-9 (nine sections, 318 rows reconciling to 952.0h, all 361 sweep ids present or folded, zero cycles, zero backward cross-phase edges, critical path recomputed) left these bookkeeping items. None blocks a phase; fix them opportunistically or fold them into `BLD-39`.

1. **half-resolved** — critics.json lens 2, finding 11 (estimate basis / repeat counts) is only half resolved. Section 1 now states the basis explicitly ('Hours are for one developer and include the stated verification ... Every verification with a repeat count of ten or more has that bench time inside its estimate'), but not one of the tasks the critic named had its estimate raised or its repeat count reduced, so the new sentence is an assertion laid over unchanged numbers rather than a re-check. _Evidence: wbs_v2.md:11 (estimate basis) versus the unchanged rows: DISP-24 = 1.5h for 'Splash exit precedes the first screen change on 20 cold boots' (line 481); APP-18 = 3h for '20 boots' (522); SCRN-02 = 3h for 'swipe right x20'_
2. **introduced by the revision** — The section 2.1 merge register is not machine-parseable in the way BLD-39 requires: two Folded cells carry parenthetical *survivor* references, so a literal parse reads DISP-01 and NET-31 as folded even though both are live rows in the tables. BLD-39's verification claims it 'flag[s] unknown ids' and 'exits 0 on this document' - it cannot do both against this register. _Evidence: wbs_v2.md:75 '| C++ host harness and fakes | BLD-06 | MDL-05, SCRN-06 (NET-08's `FakeTransport` + poller suite survive as **NET-31**) |' and wbs_v2.md:96 '| Boot heap baseline | APP-02 | PERI-01, MEDIA-01 (`DISP-01` keep_
3. **introduced by the revision** — Section 1 and section 4 both miscount how many design/judge tasks live in section 3: they name three (UIX-01, UIX-02, UIX-10) but UIX-06 is also a section-3 row. The 318/952.0 totals are unaffected (24 UIX rows in section 4 + 4 in section 3 = the 28 claimed), only the prose is wrong. _Evidence: wbs_v2.md:7 'every §4 task is Phase 3 except `UIX-01`/`UIX-02` (Phase 1 floor) and `UIX-10` (Phase 1 model), which are listed in §3' and wbs_v2.md:560 'the design's other three tasks live in §3'. UIX-06 (D12 hit-test hel_
4. **introduced by the revision** — Section 8.3 item 1 states the graph baseline is captured *before* BLD-02 deletes the stale in-tree build, which contradicts the task table in both directions: BLD-03 depends on BLD-02, and BLD-03's own verification requires the archived files to name `qnob-screen`, whereas the stale March build names `qnub-screen`. The archive must come from a fresh post-BLD-02 build, which is what BLD-03 actually does. _Evidence: wbs_v2.md:908 '... the artifact `BLD-30` needs six phases later, captured before `BLD-02` deletes the stale in-tree build (`BLD-03`)'. BLD-03 row (wbs_v2.md:131): Depends on = 'BLD-02', verification = 'three `graph-basel_
5. **introduced by the revision** — `p0-baseline` now has two different tag points. BLD-03 tags it as the first task of WP0.2, but the section 8.2 P0 gate tags it only after all of WP0.2. The tagged commit therefore does not contain APP-02's and DISP-01's instrumentation, BLD-38's threshold table, or BLD-45's build-script change - i.e. it is not the tree the baseline numbers were measured on. _Evidence: wbs_v2.md:131 BLD-03 task text '...; tag `p0-baseline`', verification 'commit tagged', Depends on 'BLD-02'. wbs_v2.md:894 P0 gate: '`SCRN-01`, `INPT-01`, `NET-01`, `APP-02`, `DISP-01` captured and transcribed; three `gra_
6. **introduced by the revision** — Section 7's new claim that BLD-06 'gates 21 test tasks' is not derivable from the Depends-on columns the section says it was derived from, and five host-test-bearing tasks have no dependency path to the harness and fakes they need. This is the residual of critics lens-2 finding 6: the three new suites (MDL-40, MDL-41, NET-31) correctly declare BLD-06, but the pre-existing host-test tasks were not brought into the same pattern. _Evidence: wbs_v2.md:855 '`BLD-06` (host harness, 4h, reach 14.0h) is not on the path but gates 21 test tasks'. Computed from the tables: 8 tasks depend on BLD-06 directly and 52 transitively, of which roughly 16 are test suites - _
7. **introduced by the revision** — The BLD-41/42/43/44 phase-edge scheme (which resolves the Phase-7-only REQUIRES blocker) leaves three residual holes, and BLD-39's lint cannot catch any of them because it only asserts `dep.phase <= task.phase`. UIX-08 creates a new .cpp in a directory BLD-43 is the task that puts into SRCS, with no edge to it; APP-21 introduces a new ESP-free leaf with no component directory and no CMakeLists in its Files column at all; and BLD-41's Files count disagrees with its own task text. _Evidence: UIX-08 (wbs_v2.md:572) Files 'ui/theme/TextFit.{h,cpp}', Depends on 'SCRN-05' - and SCRN-05 (568), which creates `ui/theme/Typography.h`, depends on 'SCRN-01, UIX-01, BLD-03, BLD-38'; BLD-43 (322) is what 'add[s] `ui/the_
8. **introduced by the revision** — SCRN-05's interim 14px font pins are specified as 'each pin deleted by that screen's own split task', but the ModeController pin has no owner whose Files touch the file it lives in. SCRN-14 is the only task that says it deletes a pin, and SCRN-14 does not edit ModeController.cpp; ModeController is retired by SCRN-16 in Phase 4, which never mentions the pin. _Evidence: wbs_v2.md:568 SCRN-05 Files include 'ModeController.cpp' and task text '(each pin deleted by that screen's split task)'. wbs_v2.md:588 SCRN-14 task text '...; delete the interim 14px pin', Files 'controllers/screens/Mode_
9. **introduced by the revision** — Section 1's new hardware-inventory rule requires every verification that needs a board other than the C6 plus knob to be marked, but two Phase-4 display tasks that need the SPI/GC9A01 panel carry no marker, so the rule is already violated by the phase it was written for. _Evidence: wbs_v2.md:13 'Verifications marked `[S3-1.75]`, `[S3-1.28]`, `[AXP2101]` or `[ES8311-capture]` need hardware the C6 does not have; if a board is absent the step is deferred and recorded, never silently skipped (§9 Q37)'._
10. **introduced by the revision** — UIX-03 edits two screen classes that sibling Phase-3 tasks delete, with no dependency edge in either direction - the mirror image of the Files-lifetime hazard the revision fixed for PERI-10 and SCRN-25. Nothing in the graph prevents SCRN-09 and SCRN-12 from running first, after which UIX-03's Files do not exist. _Evidence: wbs_v2.md:569 UIX-03 Files 'InitializationScreen.cpp, DeviceInfoScreen.cpp', Depends on 'SCRN-05'. SCRN-09 (334) 'Retire `InitializationScreen`', verification '`grep InitializationScreen` empty', Depends on 'SCRN-08'. SC_

Added after the check: `BLD-20` moved into Phase 0 ahead of `BLD-03`, which now depends on it and gates on a `git grep` for the WiFi SSID/PSK; section 1, the Phase 0/1 and WP0.1/WP1.0 headers, and the section 7 chain (root now `PCKNOB-02b -> BLD-20`, total 86.5h) were updated by hand and re-reconciled.

### Phase 0 execution notes (2026-09-12)

- `BLD-20` done. Tracked `sdkconfig.defaults.esp32c6` carries empty SSID/PSK; the bench values are in the untracked `esp-idf/sdkconfig.defaults.local`, which `build-variant.{sh,ps1}` append to `SDKCONFIG_DEFAULTS` when present; `sdkconfig.defaults.local.example` documents it. The tag gate `git grep -iE 'QNOB_WIFI_(SSID|PASSWORD)="[^"]'` passes.
- `BLD-01` done. Root `.gitignore` replaced (it was a pasted `git rm` command, ignoring nothing). `screen/SW/ESP/.gitignore` now excludes `esp-idf/sdkconfig`, `sdkconfig.old`, `managed_components/`, the bare `dependencies.lock`, the local overlay, and the same set under `screen-test/`. Stray `esp-idf/NUL` and `sdkconfig.old` deleted.
- `BLD-02` done. Stale in-tree `esp-idf/build/` (355 MB, `qnub-screen`, 2026-03-06) deleted; builds are out-of-tree under `Hardware Ref/build_*`.
- `BLD-04` decided and done: one committed lock per IDF target, `esp-idf/dependencies.lock.<target>`, passed to `idf.py` as `-DDEPENDENCIES_LOCK`; the save/restore dance is removed from both scripts. The existing lock (target esp32s3) became `dependencies.lock.esp32s3`.
- `BLD-35` decided early by `BLD-01`: `managed_components/` is not vendored; the per-target lock reproduces it.
- `BLD-37` decided: keep `esp-idf/screen-test/` as the documented bring-up app (its README already says so); its generated files are ignored by `BLD-01`.
- `BLD-39` done as `screen/SW/ESP/tools/wbs-lint.mjs` (Node, because Python is not on the Windows host and Node is required for the design tooling anyway). Clean on this document: 318 ids / 952.0h / longest path 86.5h. Exits 1 on an injected backward edge (reports the edge and the cycle it closes) and on a hand-edited section 1 total.
- `PCKNOB-02b` scaffolded: `dev_notes/credential-rotation.md` records the three credentials; rotation itself is the owner's action and is pending.
- Held for owner confirmation: `PCKNOB-01` (delete `QNOB/PC`, D16 verified in the tree) and the `p0-baseline` commit + tag (`BLD-03`).
- `BLD-03`/`p0-baseline` done 2026-09-12: commit `6392ed6` in `screen/SW/ESP` (homio-round-hmi, `codex/multi-board-support`), tag `p0-baseline`. Sizes for all three variants and the archived build descriptions are in `dev_notes/phase0-log.md`. IRAM is 16,383/16,384 bytes on both S3 boards — feed `BLD-38`.
- `BLD-45` done as `tools/check-component-graph.py`, wired into both build scripts; the C6 baseline shows `controllers -> display -> controllers` and `display -> ui -> display`.
- `BLD-04` mechanism: `-DDEPENDENCIES_LOCK` is not propagated to the build property in IDF 5.3.2; the lock is selected by `idf_build_set_property(DEPENDENCIES_LOCK ...)` in `esp-idf/CMakeLists.txt`.
- Owner decisions: QNOB references this repo as a submodule at `screen/SW/ESP`; the stale copies (`screen - Copy`, `V11 - Copy`, `screen/SW/PC`, `QNOB/PC`) stay on disk and are never added, so `PCKNOB-01` remains open.
- Primary target decided 2026-09-12: **ESP32-S3-Touch-AMOLED-1.75** (`esp32s3_amoled175`). Bench tasks (`SCRN-01`, `INPT-01`, `NET-01`, `APP-02`, `DISP-01`, `BLD-40` re-captures) run on it; `[S3-1.75]` verifications are primary, `[C6]` secondary. `PCKNOB-03` (C6 knob pin conflict) stays but drops in priority and takes the gate path (`QNOB_ENABLE_KNOB_UART` off for the C6 variant). New task **KNOB-175**: map the knob UART on the 1.75 8-pin header (`KNOB_TX_PIN`/`KNOB_RX_PIN` in the 1.75 block of `hal_config.h`, per-variant Kconfig default), verification: `+`/`-` from the V11 knob reach `DisplayController` on the 1.75. `BLD-38`: the 1.75 row is primary and gains an IRAM row (16,383/16,384 at `p0-baseline`).
- `KNOB-175` done 2026-09-12: 1.75 header `H2` = VBUS, GND, 3V3, U0TXD/GPIO43, U0RXD/GPIO44, GPIO16, GPIO17, GPIO18. Knob mapped to GPIO16 (TX) / GPIO17 (RX), avoiding the UART0 pair because the ROM boot log on GPIO43 would stall the knob firmware's line reader. Built clean on the 1.75 (+15,792 B). Bench check of `+`/`-` pending.
- `PCKNOB-03` resolved via the gate path: `CONFIG_QNOB_ENABLE_KNOB_UART` defaults off on the C6 (Kconfig + `sdkconfig.defaults.esp32c6_amoled143`); `hal_config.h` forces the knob pins to -1 when off. C6 built clean (-17,680 B). Q1 stays open.
- `UIX-01` done: `dev_notes/character-set.md` freezes D9 from the font sources (ASCII, U+00B0, U+2022, `LV_SYMBOL_*`; nothing else).

### Phase 1 execution notes (2026-09-12)

- WP1.0 code tasks done and building on all three variants: `APP-01` (`parseInt`, no bare `std::stoi`), `APP-03` (`CommandSource` trust tags + MQTT allowlist; `calibrateOrientation` is a logged no-op everywhere because the mode has no update case), `APP-04` (512 B console bound), `APP-05` (`COMMAND_TASK_PRIORITY 1`, `COMMAND_STACK_SIZE 4096` in `hal_config.h`), `APP-24`, `PCKNOB-02` (PC defaults purged, `qnob_config.json` untracked + example added), `CTRL-01`, `CTRL-02` (done at the MQTT dispatch layer: prefix strip + self-drop in `MQTTManager::handleMQTTEvent`, which also revives the dead `esp32/command` path), `CTRL-03` (gate accepts the single configured client), `CTRL-04` (25 ms publish slot, trailing flush), `NET-02`, `UIX-01`, `UIX-02` (dark theme, no grow, no transition, QR off). Details and bench checks: `dev_notes/phase1-log.md`.
- `BLD-40` (re-capture on the 1.75 under the dark theme, tag `p0-fixed`) waits for the bench. Until then the P1a gate is not closed and no tag exists.
- Deviation from the table: `PCKNOB-02` was done before `PCKNOB-01` and `PCKNOB-02b` (its listed deps): the owner keeps the stale PC copies on disk and rotation is theirs; purging the canonical copy first only removes exposure from new commits.
- WP1.1 done (owner go-ahead before `BLD-40`; additive, no behaviour change): `components/model/` (dual-mode CMake, `ModelTypes`, `Clock`, `Observable`, `MutationQueue` per D1/D18 with injectable lock/sleep/log/guard hooks, `ModelSnapshot` seqlock per D2, `AppModel` skeleton), `components/adapters/` registered per D15, BLD-41 edges added, host harness at `esp-idf/test/host` (runner, fakes, `HeapGuard`, purity guard, glob registration, sanitizers, `run-host-tests.{sh,ps1}`). Suites `test_observable`, `test_mutation_queue`, `test_model_snapshot` green under ASan/UBSan. `ModelUpdate` is 16 B, not the 20 B estimated: queue = 1,024 B. `MDL-30` completes with WP1.3. Details: `dev_notes/phase1-log.md`.
- WP1.2 done (2026-09-12): `model/Bands`, `model/ColorMath`, `model/DateFormat`, `model/RateLimit`, `model/PowerRules` and the ESP-free `command_handler/CommandParsers` leaf, each with a host suite (`test_bands`, `test_domain_helpers`, `test_ratelimit`, `test_power_rules`, `test_command_parsers`); `EnvironmentInfoScreen`, `DeviceInfoScreen`, `LightController` and `BatteryHandler` call the model and keep no numeric rule. The −20..50 °C sweep reproduces the old temperature palette bit-for-bit; the hue table matches over 361 × 21 points; the SoC curve is the same table and arithmetic. Visible changes, all deliberate: the WiFi label can show four bars (the old table stopped at three); an all-samples-failed ADC read reports invalid rather than the previous filtered voltage; `parseInt` rejects leading whitespace; `URL:mqtts://…` now parses. `PCKNOB-06` delivered as `esp-idf/PC/src/pc_media_protocol.py` + `esp-idf/PC/tests/` (16 cases, pytest and unittest, no Windows packages); its PCKNOB-01 dependency stays open by owner decision. Residual: `battery_handler → model` is a bridge edge until `PowerModel` (MDL-25) owns the percentage. Details: `dev_notes/phase1-log.md`.
- WP1.3 done (2026-09-12, owner go-ahead; additive, nothing constructs `AppModel` yet): `model/ModelBase` (state + dirty + commit-on-change + sealed subscriptions + seqlock snapshot), `AppScreen`/`KnobMode` (no SLEEP, D8), `MediaModel` with the UIX-10 amendments, `LightModel` (single wire-scale rule), `ClimateModel` (D4), `EnvironmentModel` (civil time from an epoch, 0.5 °C hysteresis with the first write admitted), `ConnectivityModel`, `PowerModel`, `NavigationModel` (single-consumption request, one activity clock, monotonic boot progress), `KnobModel` (D5 delta conversion, UIX-11 accumulator, write path with ack/timeout, stale-burst drop while a write is pending — `mode_switch_does_not_leak_setpoint` is green), and `AppModel` routing/ticking/committing with `wireSubscriptions()` sealing. Nine new suites; 17 executables / 120 cases green. Host `sizeof(AppModel)` 4,248 B (≈3.7 KB estimated on the 32-bit targets, inside MDL-01). Residuals: string transport for track metadata (MEDIA-04/05), the WP1.2 battery bridge edge (PERI-06), the knob link timeout placeholder (PCKNOB-17). Section 8.2 P1b allow-list gained the WP1.2 deltas. Details: `dev_notes/phase1-log.md`.
- WP1.4 done (2026-09-12, owner go-ahead, before `p0-fixed`): `AppModel` live in `main.cpp` with `adapters/ModelHost` hooks (APP-06); shadow records from the MQTT lambda, `DisplayController` (knob tokens incl. the dropped `setpoint=N` burst, navigation, activity, boot progress), `SoundController` and `LightController` (MDL-31, INPT-05, CTRL-07, CTRL-08); `ModelTrace` compile-gated by the new `CONFIG_QNOB_MODEL_TRACE` with the off-task write counter (MDL-32); one internet owner with an injected MQTT predicate (NET-09); fixed-array strings in `TimeHandler`/`WiFiManager` (NET-10); `ConnectivityPoller`, `EnvironmentPoller`, `BatteryPoller` on the network task with pure cores and `test_pollers` (NET-06/07/31, PERI-06); real AXP2101 presence and the dead telemetry gone, `isCharging()` kept because the PMU path populates it (PERI-05/08); battery acquisition on the network task (PERI-07); screen callbacks on the model (NET-11); weather key/city/country and the clock offset + NTP servers in Kconfig, HTTPS via the cert bundle, instance-owned buffer with chunked handling and truncation detection (NET-28/29); no blocking call left on the network task: non-blocking WiFi connect sequence, NTP, async HTTPS, non-blocking internet probe (NET-30); `knob_link_protocol.md` (PCKNOB-04); `last_setPoint` deleted in the sketch, uncommitted in QNOB (PCKNOB-05). DST decision (section 9 Q28): fixed offset, no DST. Host suite 19 executables / 128 cases green. Details: `dev_notes/phase1-log.md`.
- Added on owner request 2026-09-12: `APP-28` (persisted runtime fields through a model subscriber, Phase 5, 5h) and `APP-29` (one writer for the settings store, Phase 6, 3h); section 1 totals are now 320 tasks / 960.0h. Both came out of the WP1.4 persistence review: runtime state that should survive a reboot stays model-owned and NVS subscribes; configuration stays a store with a single writer.
- Bench 1 on the 1.75 (2026-09-12/13, `dev_notes/phase1-log.md`): the INFO screen halted in an LVGL out-of-memory assert at 466 px (fixed with `CONFIG_LV_MEM_CUSTOM=y`, LVGL error logging on); first clean pass with every drain `offtask=0`; three open findings (SleepHandler 120 MHz retry flood, bars hysteresis, two early rejected records). Owner decision 2026-09-13: this branch stays the base and the screen layer of `main` (homio-round-hmi, 123 commits past the 2026-04-21 fork, clean on the board) was ported file by file with the WP1.4 posts and the WP1.2 lifts re-applied; the 1.75 runs the panel at rotation 0 with touch mirror X/Y (the Phase 0 270-degree software rotation was a transcription error). **The visual reference for every later `p0-fixed` comparison is the ported main screen set**, not the pre-port screens; `MATRIX_PAGE` placeholders post no `ActiveScreen` until Phase 3. The C6 and 1.28 were dropped from this pass on the owner request.
