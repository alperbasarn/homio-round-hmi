# Phase 0 log — Baseline

Executed against `dev_notes/mvc_refactor_wbs.md` Phase 0. Started 2026-09-12.
Repository facts that changed the plan: this tree (`screen/SW/ESP`) is its own
git repository, `github.com/alperbasarn/homio-round-hmi`, public, branch
`codex/multi-board-support`; the outer `QNOB` repository does not reference it.
Firmware commits and the `p0-baseline` tag belong here, not in QNOB.

## WP0.1 Rotate, blank, then freeze the tree

| Task | Status | Evidence |
|---|---|---|
| PCKNOB-02b rotate credentials at providers | **pending — owner** | `dev_notes/credential-rotation.md`. WiFi PSK is public on this repo since `84e66e8`; HiveMQ on both repos; OWM key on QNOB. |
| BLD-20 blank WiFi defaults, overlay for bench values | done | `sdkconfig.defaults.esp32c6` SSID/PSK empty; values in ignored `esp-idf/sdkconfig.defaults.local`; `.local.example` added; both build scripts append the overlay. Build log: generated `sdkconfig.esp32c6_amoled143` carries the SSID, tracked defaults blank. Gate `git grep -iE 'QNOB_WIFI_(SSID\|PASSWORD)="[^"]'` on tracked-candidate files: clean. |
| BLD-37 decide `screen-test/` | done | Keep as the documented bring-up app (README already says so); its generated files ignored. |
| BLD-01 ignore rules, stray files | done | Root `.gitignore` was a pasted `git rm` command; replaced. `screen/SW/ESP/.gitignore` gained `esp-idf/{sdkconfig,sdkconfig.old,managed_components/,dependencies.lock,sdkconfig.defaults.local}` and the `screen-test/` equivalents. `esp-idf/NUL` and `sdkconfig.old` deleted. `git check-ignore` confirms each path. |
| BLD-02 delete stale in-tree `build/` | done | 355 MB, project `qnub-screen`, 2026-03-06, removed. Fresh out-of-tree C6 build reports `qnob-screen`. |
| BLD-04 lock policy | done | One committed lock per IDF target, `esp-idf/dependencies.lock.<target>`, selected by `idf_build_set_property(DEPENDENCIES_LOCK ...)` in `esp-idf/CMakeLists.txt` (a `-D` cache variable is not propagated to the build property in IDF 5.3.2; verified in `tools/cmake/build.cmake:537`). Save/restore dance removed from both scripts. Two consecutive reconfigures: no bare lock created, `dependencies.lock.esp32c6` unchanged, git status shows only the per-target locks. |
| BLD-39 WBS lint | done | `tools/wbs-lint.mjs` (Node). Real document: 318 ids / 952.0h / longest path 86.5h, clean. Injected backward edge: reports the edge and the cycle it closes, exit 1. Hand-edited section 1 total: exit 1. |
| PCKNOB-01 retire `QNOB/PC` | **held — owner** | D16 verified: `esp-idf/PC` is newer (2026-04-08 vs 2026-01-25), adds `ota_helper.py`, defines `send_command` on the app class; `QNOB/PC` calls it 15 times without defining it. A third copy exists at `QNOB/screen/SW/PC` (untracked). |

## WP0.2 Measure, archive, tag

| Task | Status | Evidence |
|---|---|---|
| BLD-03 size per variant, graph archive, tag | done | All three variants measured (below); `dev_notes/graph-baseline-{esp32c6_amoled143,esp32s3_lcd128,esp32s3_amoled175}.json` archived; tag `p0-baseline` on the owner's go-ahead (2026-09-12). |
| APP-02 heap logging | not started | needs a code change and a flash; bench. |
| DISP-01 draw-buffer geometry | not started | bench log. |
| SCRN-01 screenshots, INPT-01 gestures, NET-01 contract capture | not started | bench. |
| MDL-01 RAM budget, BLD-38 threshold table | blocked on APP-02 and the S3 numbers | |
| BLD-45 component-graph dump | done | `tools/check-component-graph.py` (Python, runs in the IDF venv inside both build scripts, `\|\| true`). On the C6 baseline: 22 first-party components, 55 edges, and exactly the two cycles the plan names: `controllers -> display -> controllers` and `display -> ui -> display`. |

### BLD-03 measurements — esp32c6_amoled143 (2026-09-12, HEAD 6332b09-dirty)

| Measure | Value |
|---|---|
| `qnob-screen.bin` | 1,748,000 bytes (0x1AAC20); smallest app partition 0x320000; 47% free |
| Flash code (.text + .rodata + .eh_frame) | 1,607,908 bytes |
| DIRAM used / remaining / total | 233,299 / 218,813 / 452,112 bytes (51.6% used) |
| DIRAM .bss / .data / .text | 93,424 / 16,033 / 123,842 bytes |
| Bootloader | 0x5A80 bytes, 29% free |

Largest archives (`size-components`, total size): `liblvgl__lvgl.a` 278,311;
`libmbedtls.a` 69,488 + 33,728; `libmedia.a` 53,963; `libui.a` 20,972;
`libmqtt.a` 17,736; `libmain.a` 14,057; `libcontrollers.a` 12,435;
`libdisplay.a` 6,473; `libmqtt_manager.a` 4,604.

### BLD-03 measurements — esp32s3_lcd128 and esp32s3_amoled175 (2026-09-12, HEAD 6332b09-dirty)

| Measure | s3_lcd128 | s3_amoled175 |
|---|---|---|
| `qnob-screen.bin` | 1,542,656 bytes (0x178A00); 53% of 0x320000 free | 1,568,656 bytes (0x17F010); 52% free |
| Flash code | 1,065,874 | 1,086,434 |
| DIRAM used / remaining / total | 205,907 / 135,853 / 341,760 (60.25%) | 203,519 / 138,241 / 341,760 (59.55%) |
| .bss / .data | 86,968 / 21,664 | 87,352 / 21,568 |
| **IRAM** | **16,383 / 16,384 (99.99%)** | **16,383 / 16,384 (99.99%)** |
| Bootloader | 0x59C0, 30% free | 0x59C0, 30% free |

IRAM is full on both S3 boards. Any change that adds IRAM-resident code
(ISR handlers, `IRAM_ATTR`, flash-cache-disabled paths) will fail those builds;
the C6 has no separate IRAM pool. Feed this into `BLD-38`'s threshold table.
Both S3 builds used `dependencies.lock.esp32s3` unchanged (mtime 2026-04-10) and
created no bare lock.

### Warnings to hand to a later BLD task (not Phase 0)

`idf.py reconfigure` on the C6 warns that `SPIRAM_MODE_QUAD` and
`SPIRAM_SPEED_80M` (in `sdkconfig.defaults` and `.esp32c6`) and
`ENABLE_WIFI_STATION` (the Matter block in `.esp32c6`) are unknown Kconfig
symbols. Harmless; removing them is behaviour-neutral but belongs with the
sdkconfig cleanup, not with the baseline.

## Decisions taken

- BLD-35 (vendor `managed_components/`?) — no; the per-target lock reproduces it.
- Lint is Node, not Python: Python is not on the Windows host; Node is required by the design tooling.

## Held for the owner

1. Commit and tag `p0-baseline` in `screen/SW/ESP` (this repository).
2. How `QNOB` references this repository (submodule at `screen/SW/ESP`, or hardware-only with a README pointer), and dropping QNOB's stale `screen/SW/esp-idf` + `screen/SW/arduino` paths.
3. Deleting the stale copies: `QNOB/screen - Copy` (7,206 files, 1.1 GB, 4,526 of them build outputs), `QNOB/V11 - Copy` + `V11 - Copy.zip`, `QNOB/screen/SW/PC`, `QNOB/PC` (PCKNOB-01).
4. Credential rotation and the history-rewrite question.
