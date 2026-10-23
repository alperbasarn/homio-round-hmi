# Handover: the MVC refactor of the QNOB screen firmware

Written 2026-10-23 for whoever picks this up next, including a future session with no memory of
the work. It says where the project stands, what is trustworthy, what is owed, and the one thing
that must be decided before the branch can rejoin its remote.

Read this first, then `mvc_refactor_plan.md` (the architecture and the numbered decisions D1-D21),
then `mvc_refactor_wbs.md` (336 tasks, 1022 h; section 9 holds the owner's open questions), then
the phase logs. Every work package that landed has a section in its phase log naming what it did,
how it was verified, and every deviation from the row it claims.

## 1. What the refactor is

Six screen classes each polled the touch panel, owned their own timers, parsed MQTT, classified
their own domain values and drew LVGL. The refactor splits that into

- **model** (`components/model`): the state. One writer, the display task. Everything that crosses
  a task boundary arrives as a 16-byte record through `MutationQueue` and is drained once per
  frame. No platform includes; a purity guard in the host tests enforces that.
- **view** (`components/ui/views`): LVGL handles only. Built with an injected panel size, rendered
  from a state struct, change-driven, no allocation per frame, the LVGL lock a caller precondition.
- **controller** (`components/controllers/screens`): input interpretation, timing, model mutation
  and navigation, through the `ScreenController` contract and a polled navigation outbox.
- **adapters** (`components/adapters`, `components/media_target`): transports. Pollers and bridges
  post records; the pure cores compile on the host.

## 2. Where it stands

| Phase | Scope | State |
|---|---|---|
| 0 Baseline | measurements, `p0-baseline` tag, defect and dead-code registers | done |
| 1 Floor, then model | the model layer, the queue, the bands, the host harness | done, bench-verified |
| 2 Unify input | one `InputEvent`, `InputRouter`, knob line assembler, screens on `onInput` | done, bench-verified |
| 3 Split screens | 120 tasks: a view and a controller per screen, plus the LED and media clusters | about half landed, **no bench verification since 2026-09-13** |
| 4 Router | `ScreenRouter` replaces the `DisplayController` switch; the mode wheel splits | not started |
| 5 Transport out of views | every MQTT edge into a bridge | not started |
| 6 Composition root | `main.cpp` strangled, settings persistence | not started |
| 7 CMake tightening | edges enforced, dead code deleted | not started |

Landed in phase 3: the view and controller contracts (WP3.0), the boot screen (WP3.1), the
DEVICE_INFO split (WP3.2), the LED strip integration (LED-01..05: registry model, topic contract
v1, blink-code pairing, the LIGHT screen on the registry), the media target layer (MEDIA-02..10),
the MEDIA screen with the pure knob binding machine (MEDIA-13..16), and the screen side of the
knob write path (PCKNOB-15 and half of PCKNOB-16).

Three legacy classes are deleted outright: `SoundController`, `DeviceInfoScreen` and the old
`LightController`. `EnvironmentInfoScreen` (911 lines), `ModeController` and
`PlaceholderPageScreen` remain.

## 3. What is trustworthy, and what is not

**Verified by machine, every commit:** 34 host suites under ASan and UBSan with `-Werror` and a
no-heap guard on the render paths; the 1.75 firmware builds (0x1cd4e0, 42 % of the app slot free);
`tools/check-include-edges.py` reports 25 components with no undeclared include edge; the WBS lint
is clean; two credential greps are empty before every commit (section 6).

**Verified on the board:** everything up to and including phase 2, plus the stage-3 sleep gate and
the boot path. The last bench session was 2026-09-13.

**Not verified on the board at all:** every phase-3 screen since then. `dev_notes/bench_runbook.md`
is the session script in order, A1 through A7 plus the strip steps. Nothing in it has been run.
Until it has, treat the DEVICE_INFO, MEDIA and LIGHT screens as code-complete and unproven.

Two review rounds with independent agents (four readers and a critic mapping the old code, then
five review dimensions with every finding handed to a second agent to refute) found nineteen real
defects before their commits. The findings and the refutations are recorded in `phase3-log.md`.

## 4. The branch problem, which blocks rejoining the remote

The firmware branch `codex/multi-board-support` **has diverged from its own remote**. As of
2026-10-23:

- our side: 39 commits, the entire refactor, forked at `6332b09`
- the remote's side: 14 commits we do not have, pushed after the fork

The 14 are real work and must not be discarded: `df65ff9 add ota support`, the OTA release
pipeline and GitHub Pages config, `caf8c49` the configurable OTA variant and software-update UI,
the setup portal commits, `659bb1c` wiring the display controller to the LVGL display lifecycle,
`85704c6` the init/mode/device-info LVGL screen flows, `ea75d5a` the UI drawing primitives, and
`47e790b` the environment arc intro and outro animation.

Those touch exactly the files the refactor rewrote: `DisplayController`, `main.cpp`, the screen
classes. A merge is a real integration with real conflicts, not a formality, and it overlaps the
WBS OTA cluster (`OTA-01..07`, "port the LED project's OTA flow"), which may now be partly
redundant against `df65ff9`.

**Therefore this work is pushed to its own branch, `refactor/mvc-phase3`.** Nothing on the remote
is touched or lost. The integration is the owner's call, and the options are:

1. Merge `origin/codex/multi-board-support` into the refactor branch and resolve, reconciling the
   OTA cluster against the OTA work already on the remote. This is the honest path and should be
   scheduled as its own work package with its own bench gate.
2. Rebase the refactor onto those 14 commits. Cleaner history, but 39 commits replay over rewritten
   files; expect the same conflicts spread across every replay.
3. Treat the remote branch as superseded and merge the other way. Only the owner can say whether
   the portal and OTA work there is still wanted.

Do not force-push `codex/multi-board-support` under any circumstances.

The QNOB superproject branch `task/esp-idf-migration` has not diverged: 38 commits ahead, nothing
behind, a clean fast-forward. Each firmware commit is followed by a submodule-pointer commit there.

## 5. How to build and test

The toolchain lives in WSL under the `buildserver` user. From `screen/SW/ESP`:

```bash
bash run-host-tests.sh                        # 34 suites, ASan/UBSan, the purity guard
bash build-variant.sh esp32s3_amoled175       # the bench board, out-of-tree build
python3 tools/check-include-edges.py esp-idf  # component edges
node tools/wbs-lint.mjs                       # the work breakdown
```

From Git Bash on Windows, wrap each in `wsl -u buildserver -- bash -lc '...'` with literal paths;
`$VAR` and `$(...)` do not survive the outer shell. The bench scripts in `tools/bench/` (flash,
capture, boot_check, sleep_check, diag, led_check) are the repo-resident versions of the session
scripts and write to `Hardware Ref/bench_logs`, which is ignored; they scrub the WiFi PSK and exit
2 without a port.

Other tooling quirks that bite, recorded because they cost hours: a heredoc over about 8 KB is
truncated mid-command; a backslash before a quote or a percent sign collapses, so a `\n` inside a
C string written through a script becomes a real newline and breaks the build (this happened to
`KnobController.h` and was caught by the compiler); files that git has touched may be CRLF, so
normalise before a regex edit; `.gitattributes` pins LF for the scripts that run in WSL.

## 6. The credential gate, which is not optional

Three secrets are already public in both remotes' history: the bench WiFi PSK, the HiveMQ
credentials and an OpenWeatherMap key. **Rotation is still pending and is the owner's to do**
(`dev_notes/credential-rotation.md`; open question 36 decides whether history is rewritten or the
values are accepted as burned). Until then: never print the PSK, never use the leaked weather key,
and run both greps against the index before every commit, each of which must come back empty.

```bash
git grep --cached -iE 'QNOB_WIFI_(SSID|PASSWORD)="[^"]' -- . ':!*.example'
git grep --cached -n '<first 8 hex of the leaked weather key>' -- .
```

The second grep's needle is deliberately not written anywhere in this tree, this file included:
the gate greps the index, so pasting the literal here would make every future commit fail its own
documentation. It was caught doing exactly that while this handover was being committed. Take the
value from the key the owner is rotating, or from the shell history of an earlier session.

The bench values live in `esp-idf/sdkconfig.defaults.local`, an untracked overlay that also turns
the model trace on and both sleep modes off.

## 7. What is owed, in the order it should be picked up

1. **A bench session.** `bench_runbook.md` A1 to A7 and the strip steps. Everything in phase 3
   since 2026-09-13 is unproven, and A2, A5, A6 and A7 are first looks at new screens.
2. **The second USB-drop cause.** A build with deep sleep compiled out still dropped off the bus
   while the port sat idle. Diagnostics are committed and waiting: an RTC no-init black box
   rewritten every 30 s and the AXP2101 power-off reason at boot. After the next drop press RST,
   not re-plug, then run `diag.sh <log> 60 --no-flash` and read the `sysInfo:` block.
3. **The branch integration** of section 4.
4. **WP3.3**, the environment screen. It starts with `SCRN-02`, a leave-animation hotfix that
   ships alone, and its middle holds `SCRN-28`, a lifecycle measurement that needs a 50-cycle soak
   on the board and decides D17 for every view's `destroy()`.
5. **The LED strip repo.** `esp32c6-led-web` branch `feat/mqtt-controllers` is two commits ahead of
   its pushed `dev` branch and has never run on hardware. Its app slot is 99 % full.

## 8. Decisions waiting on the owner

The whole list is section 9 of the WBS; these are the ones blocking scheduled work.

- **Q4, may the knob firmware change?** Blocks the knob-side half of the write path: the
  acknowledgement itself, ping/pong liveness, the haptic setter validation and the `calibrate`
  passthrough. Until then a write rides the model's 1 s timeout, which is today's behaviour plus
  the push.
- **Q39 and the design choice**, whether LIGHT gets the Instrument-shell redesign, and Instrument
  versus Shell in general. The design canvas is a published artifact; the choice is unmade, and
  D19 currently exempts LIGHT.
- **Q36**, git history rewrite versus accepting the burned credentials.
- **Q31 and Q37**, which boards are shipping and which are physically on the bench. Roughly twenty
  verifications in the WBS depend on the answer.
- **Q1**, whether the J16 header is wired to GPIO 1 and 2 on the built boards. No software change
  reconciles the knob UART with PWR_KEY and the touch-wake line if it is.
- The `p0-fixed` screenshots, which several view rows compare against and which only the owner can
  take.

## 9. Repository map

- `screen/SW/ESP` — this repo, `alperbasarn/homio-round-hmi`, a submodule of `alperbasarn/QNOB`.
  Run git here, not in the superproject, for firmware work. Other copies of the tree on disk
  (`screen - Copy` and friends) are stale and must not be edited.
- `esp-idf/` — the live firmware tree: `components/`, `main/`, `test/host/`, `PC/` (the companion
  app and its pytest suite), `tools/`.
- `dev_notes/` — the plan, the work breakdown, a log per phase, the bench runbook, the gesture
  table, the LED MQTT contract and its strip-side review, the credential rotation note, and the
  character set.
- `Hardware Ref/` — the out-of-tree build directories, the generated sdkconfigs per variant, and
  the ignored bench logs.
