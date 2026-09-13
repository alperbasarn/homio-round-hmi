# Phase 2 execution log — Unify input

Rows and hours are the WBS (`mvc_refactor_wbs.md`, Phase 2). Each work package lists what was done,
what deviates from the row text and why, and the on-device evidence. The bench board is the
ESP32-S3-Touch-AMOLED-1.75; the C6 and 1.28 variants are not built in this phase (owner
instruction 2026-09-12, "drop those variants").

## WP2.0 Component edges and the LocalAudio move (2026-09-13)

| Task | State | What changed | Evidence |
|---|---|---|---|
| INPT-06 `InputEvent` | done | `components/input/include/input/InputEvent.h`: `InputType` (None, TouchPress, TouchMove, TouchRelease, TouchHoldRelease, Gesture, KnobDelta, KnobLink), `Gesture` (the four swipes, single tap, double tap, long press), `InputEvent{type, gesture, x, y, knobDelta, durationMs}`; `durationMs` last so the record packs into 12 bytes; `toString()` for both enums so INPT-12 can log every event; the header states that the knob has no push button. | `static_assert(sizeof < 16)` in the header and in `test_input_event` (host); compiles in the IDF build |
| BLD-11 `components/input` | done | Registered with `REQUIRES model`, `PRIV_REQUIRES touch_panel knob_controller esp_timer`; `main` names it. The public surface is one header and one translation unit until INPT-07 adds the router. | `check-include-edges.py` clean; the BLD-45 dump lists `input` |
| PERI-11 `MediaController` -> `LocalAudio` | done | `git mv` into `components/local_audio/LocalAudio.{h,cpp}` (its own component, `REQUIRES media`), class and log tag renamed, the eight call sites updated in one commit: `CommandHandler` (forward declaration, `registerLocalAudio`, member), `DisplayController` (same), `main.cpp` (construction, two registrations), the `controllers` manifest lost the source and its `media` edge. | `grep -r MediaController components main test` is empty; `local_audio` appears in the graph dump; the three recording commands still route (`startSoundRecord` / `stopSoundRecord` / `playLastSoundRecord` in `CommandHandler`) |
| BLD-42 phase edges | done, additive | `input` added to the `REQUIRES` of `controllers`, `ui`, `display` and `main`; `local_audio` replaces the `media` edge of `controllers` and is named privately by `display` and publicly by `command_handler` and `main`; `main` keeps `media` because it still constructs `SDCard`, `Speaker`, `Microphone`, `SoundPlayer` and `SoundRecorder`. The `display` -> `controllers` include path stays until PERI-09 removes the include (WP2.1). | `tools/check-include-edges.py`: 24 components, 0 undeclared include edges; 1.75 build clean |
| PERI-12 dead media API | done | Deleted `SoundPlayer::playReactionSound`, the two unreachable embedded fallbacks inside `playEffectSound` (click and release both return earlier), `SoundRecorder::recordToFile`, `Microphone::{set,get}SoftwareGain` with the gain loop and its constants, `SDCard::{readFile,writeFile,writeTextFile}`, `logDirectoryTree` (unused, was a build warning) and the 317-line `kStartupSound` table (`playStartupSound` synthesises its chord). `LocalAudio` lost `isRecording()`, `getLastRecordingPath()` and the two static file-name getters (now file-local). The WAV/MP3 same-basename fallback and the directory scan stay: they are reachable when an SD card is mounted. | 1.75 binary 1,829,136 B against 1,829,504 B before WP2.0 (-368 B: the table was `constexpr` and the functions unreferenced, so the linker had already dropped most of it); the six embedded effects take the same code path as before |

Host suite after WP2.0: 20 executables, 130 cases, all green under ASan/UBSan (`test_input_event` is new).

Deviation: only the 1.75 is built; the BLD-42 row asks for a `fullclean` build on all three variants. Flashing waits for the board, which had gone into its 3-minute deep sleep and off the USB bus (it wakes on a tap now).

Graph after WP2.0 (`tools/check-component-graph.py` on the 1.75 build): `input -> model` public,
`input -> touch_panel` and `input -> knob_controller` private; `controllers`, `display`, `ui` and
`main` -> `input` public; `command_handler` and `main` -> `local_audio` public, `display` ->
`local_audio` private; `local_audio -> media` public; `main -> media` kept. The two first-party
cycles of the Phase 0 baseline (`controllers <-> display`, `display <-> ui`) are unchanged; Phase 7
removes them.

## WP2.1 InputRouter (2026-09-13)

| Task | State | What changed | Evidence |
|---|---|---|---|
| INPT-07 `InputRouter` | done | `components/input`: `InputTranslator` is the pure core (8-entry fixed ring, overflow and high-water counters, activity clock); `InputRouter` is the device wrapper that polls `TouchPanel` and `KnobController` once per frame on the display task and feeds the core. The driver headers stay out of the public headers (forward declarations; `input` publicly requires only `model`). | `test_input_translator`: 12 tokens into an 8-slot ring give `pending 8`, `overflow 4`, order kept; the 60 s stress walk on the device is the owner's (board off the bus, see below) |
| INPT-08 translation | done | The seven latched flags become `press` (with hit zone), `move` every poll while down, `release` with `durationMs`, `hold-release` only at 3000 ms or more (D13; shorter ones are consumed), the four swipes plus the hardware taps; software double-tap counter (1000 ms) and a 250 ms stale window (`beginStaleWindow`) that drops the release or gesture belonging to the press that changed the screen. `durationMs` is a saturating `uint16_t` so the record stays 12 bytes. | host cases: tap sequence, 1 s hold discarded / 3.5 s passed, double-tap, stale window |
| INPT-11 activity + wake | done | `armWakeConsumption()` drops the next press and its release; `lastActivityMs()` still moves for consumed events so the display wakes. `main.cpp` arms it on the frame the mode becomes SLEEP and forwards every activity tick to `DisplayController::noteActivity()` and `SleepHandler::registerActivity()`. | host case: the armed press and release yield no event, the next tap yields two |
| INPT-12 dual-path wiring | done | `main.cpp` owns `InputRouter` and `InputSfxListener`; the frame is drain -> `poll` -> events (one `input ...` log line each, SFX, knob records into the model, `DisplayController::onInput`) -> `update()`. The legacy screens still read the latched flags in the same frame (WP2.3). The INPT-05 knob posting moved here from `DisplayController` (`Delta`, `Burst`, `LinkActivity`), so the model sees each token once. | build; the checklist walk on the device is pending |
| UIX-06 hit zones | done | `input/HitZones.h`: `classifyHit()` with the D12 radii (disc r < 132, or 145 on climate; rim 140..190; the rest dead) in 466-px design space, scaled by the shorter side; every touch event carries its zone and the log prints it. | host case incl. the 240 px scaling; `LV_OBJ_FLAG_CLICKABLE` appears only in `clear_flag` calls on arcs and on root containers |
| PERI-10 touch feedback | done | `input/ITouchFeedback.h` (press, release, swipe); `LocalAudio` implements it (`local_audio` requires `input`, no edge out of `input`). | `check-include-edges.py` clean; graph dump shows `local_audio -> input` only |
| INPT-13 SFX listener | done | `InputSfxListener`: click at finger-down, release sound at finger-up, one swipe effect per swipe; `DisplayController` lost its debounced sound block. | host case: press 1 / release 1 / swipe 1, move and double-tap silent |
| INPT-14 DisplayController input block | done | The polling block, the touch-sound members, `touchPanel`, `knobController`, `localAudio`, `registerKnobController`, `registerLocalAudio` and `postKnobToken` are gone; `DisplayController()` takes no driver; `onInput()` routes swipes to the matrix (not on Device Info) and knob deltas to the mode (SOUND/LIGHT setpoints, HOME wheel with the `ModeStep` record); `noteActivity()` leaves SLEEP. | `grep -E "touchPanel\|knobController\|localAudio" DisplayController.*` is empty; build clean |
| PERI-09 activity to the sleep handler | done | `display` no longer includes or requires `sleep_handler`; the Activity record is rate-limited to one per 250 ms so a drag no longer posts one per frame. | include-edge check clean; Stage 0 timing on the device pending |
| MDL-33 knob boundary | done (verified) | The router posts `Delta` / `Burst` / `LinkActivity` records; `detentCount` is written only inside `KnobModel` (`applyDelta`, `burstToDelta`, the write ack path). | `grep detentCount` outside `components/model`: none |
| SCRN-25 gesture semantics | done | `dev_notes/gesture_table.md`; SOUND and LIGHT now require the 3 s hold for back (they exited on any hold-release); the three hints say what the screens do (bullet U+2022, in the built-in font range). | table + build; the on-screen check is pending |

Host suite after WP2.1: 21 executables, 139 cases, all green under ASan/UBSan. 1.75 binary
1,831,712 B (+2,576 B against WP2.0 for the router, the translator and the listener), 44 % free,
no first-party warnings. Bench: the board went into its 3-minute deep sleep before the WP2.0
flash and is off the USB bus; the new `CONFIG_QNOB_DEEP_SLEEP` gate (off in the bench overlay)
stops that from the next flash on. Pending on the device: the checklist walk with one `input`
line per gesture, the 60 s ring stress, the wake tap, Stage 0 at 20 s, the hint strings.

## WP2.2 Knob wire format, screen side (2026-09-13)

| Task | State | What changed | Evidence |
|---|---|---|---|
| PCKNOB-09 line assembler | done | `knob_controller/KnobLineAssembler.{h,cpp}` (pure): bytes in, tokens out of an 8-deep FIFO; bare `+`/`-` complete when another sign follows or after a 25 ms hold, `+N`/`-N` complete on LF/CR, everything else is a message; lines cap at 63 bytes with a truncation counter. `KnobController::update()` reads the UART in 64-byte chunks (512 per call at most) into the assembler; the old code read the whole 512-byte ring into a 256-byte stack array and kept the first line. The last message sits in a fixed buffer behind a spinlock, so the command task copies it safely (the cross-task `std::string` copy is gone). | `test_knob_line_assembler`: `++-+` is four detents (the D6 failure was two tokens matching nothing), a line split across polls is one count, a lone sign completes after the hold, 50 detents in a second give 50 tokens with no loss when drained, FIFO overflow counts, an over-long line truncates |
| PCKNOB-11 `consumeDelta()` | done | Detents are summed into an atomic (each token clamped to +-10, D5) that the router reads and clears every frame; `+3,-1,+,+abc,+99999` give 3, -1, 1, a message, 10. A knob on the old firmware keeps working (bare signs). `InputRouter::poll` uses `consumeDelta()` for deltas and the message path for the rest; `InputTranslator::onKnobDelta()` carries the sum. | host cases; build |
| PCKNOB-07 / PCKNOB-08 / PCKNOB-10 (knob firmware) | waiting on Q4 | The knob-side rows change `FOC.ino` / `serial.ino` in the QNOB repository and need every knob reflashed; D6 lets the screen side land first, which it has. | owner decision |

Host suite after WP2.2: 22 executables, 147 cases, all green. 1.75 binary 1,832,736 B (+1,024 B).
Device: not flashed yet (board off the bus).
