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
