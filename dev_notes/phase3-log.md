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
