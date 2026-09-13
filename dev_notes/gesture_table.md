# Gesture table (SCRN-25, 2026-09-13)

One vocabulary for every screen, as the `InputRouter` emits it (WP2.1) and as the five live screen
classes still read it off `TouchPanel` until WP2.3 cuts them over. Every on-screen hint below is the
string the screen shows; the log line is what `main.cpp` prints per event (`input <type> <gesture>
x y zone d k`).

## The vocabulary

| Event | Produced when | Notes |
|---|---|---|
| `press` | finger down | carries the hit zone (`disc`, `rim`, `dead`, D12) |
| `move` | every poll while the finger is down | drags read these; nothing navigates on a move |
| `release` | finger up after a short press, within the 25 px tap radius | `durationMs` is the press length; a second release inside 1000 ms adds `gesture double-tap` |
| `hold-release` | finger up after a hold **of at least 3000 ms** (D13) | shorter hold-releases are consumed by the router and never reach a screen; **back** everywhere |
| `gesture swipe-up/down/left/right` | 40 px of travel in one direction | the matrix navigation and the wheel read these |
| `knob-delta` | one `+`/`-` (or `+N`/`-N`) token | clamped to +-10 per token (D5) |
| `knob-burst` | a `setpoint=N` token | the model converts it to a delta (D5) |
| `knob-link` | any other token | link alive, no movement |

Two guards the router applies before a screen sees anything: the **wake tap** (the press that ends
SLEEP and its release) is consumed (INPT-11), and a **250 ms entry window** after a screen change
drops the release or gesture that belongs to the press which caused the change (the screens'
`ignoreNextRelease` / `STALE_RELEASE_GUARD_MS` copies go with WP2.3).

## Per screen

| Screen | Tap (`release`) | Double-tap | Swipe | Hold 3 s (`hold-release`) | Knob | Hint shown |
|---|---|---|---|---|---|---|
| INFO (EnvironmentInfoScreen) | nothing | nothing | up: Device Info; down: first placeholder page; left/right: nothing (all pages sit on x = 0) | nothing (INFO is the root) | nothing | none (home view) |
| Device Info | bottom band: back to INFO; left edge: previous page; right edge: next page; brightness and sound bars: drag sets the value | nothing | consumed by the page (no matrix swipe on this page) | same as tap on the bands | nothing | none (buttons carry their own labels) |
| Placeholder pages (timer, light, sound, temperature, PC) | PC page: play/pause button | nothing | up/down: previous/next page | nothing | nothing | `Swipe up or down to move` |
| Mode wheel (HOME) | first tap arms; nothing visible | selects the highlighted mode | left: previous mode; right: next mode | back to INFO | +: next mode, -: previous mode (also a `ModeStep` record) | `<- swipe ->  •  double-tap selects  •  hold 3 s: back` |
| MEDIA (the SOUND mode) | disc: play/pause through the active target; while a scrub is in progress the release commits the seek and is not a tap; rim: nothing (reserved for the target overlay, UIX-28) | enter or leave SEEK when the target reports a duration and a seek resolution; otherwise a short "Not available" | left: next track; right: previous track (D14); up/down: nothing | back to INFO (a scrub in progress is discarded) | VOLUME: +-1 per detent (a VolumeLocal record and target.setVolume, coalesced to one publish per 120 ms); SEEK: 1 % per detent, one 10 % step per detent on the coarse PC path, one seekTo on release or after 450 ms idle, SEEK reverts to VOLUME after 6 s idle | `Tap play/pause  •  Knob volume  •  Swipe track` / `Double-tap seek  •  Hold 3 s back` |
| LIGHT | disc: power on/off; rim: choose that strip; while a pairing is requested: confirm the dialled count | nothing | left/right: previous/next effect (a rim drag is not a swipe) | back to INFO after a press that did not drag; on an unpaired rim segment: start pairing | brightness +-1 per detent; while a pairing is requested: the blink count 1..6 | `Tap: power  •  Knob: brightness  •  Swipe: effect  •  Ring: colour  •  Hold 3 s: back` |

Sound effects (INPT-13): click at `press`, release sound at `release` / `hold-release`, one swipe
sound per swipe gesture; nothing on a move.

## Deltas against the pre-WP2.1 behaviour

- SOUND and LIGHT exited on any hold-release; both now require 3 s (D13), like the wheel already did.
- The Sound hint said "Long release back" without a duration; the Light hint did not mention back at
  all; the wheel hint did not mention the double-tap. All three say what the screen does now.
- Device Info keeps its band/edge taps: the ported main design uses buttons there, so the matrix
  swipes are deliberately off on that page (`DisplayController` skips them while `DEVICE_INFO`).
