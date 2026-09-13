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
