# Review of the LED strip MQTT link (esp32c6-led-web, branch `feat/mqtt-controllers` = `dev`)

Read-through of the four commits (df141d5 .. 82c1c36) on 2026-09-13, after the knob side of the
contract landed. Files: `main/mqtt_link.{h,cpp}` (1083 lines), `main/mqtt_proto.{h,cpp}`,
`main/led_control.h`, the Matter callback and the web hooks in `main/app_main.cpp`, the host
tests (141 checks), `docs/mqtt-contract.md`.

## What holds up

- Threading: one publisher (the `mqtt_link` task), inbound parsed on the esp-mqtt task, callers
  only mark and notify, the link mutex never held across a publish or a `led_control_*` call.
  The Matter callback releases the state mutex before `mqtt_link_state_changed("matter")`.
- The Matter path is not a lost publish: it writes the state, persists, notifies the effect task
  and marks the coalescer with `src=matter`. It is a second clamp-and-persist path beside
  `led_control_apply_json` (structural duplication, deliberately kept off the CHIP thread; no
  change made).
- Contract behaviours verified in code: LWT retained `offline` and an explicit `offline` on a
  clean disconnect; retained `info`, `status`, `controllers`, `state` on every connect;
  `set` from an unpaired controller answered with a `state` carrying `rejected` and the sender as
  `src`; rejections published before an unrelated state change in the same window; first pairing
  free, idempotent re-pair, four controllers, blink code 1..6 with a 60 s window; persistence of
  the list in NVS, debounced `set` persistence, factory reset clearing the retained topics.
- Inbound safety: 1024 B payload cap with chunk reassembly, cJSON type checks on every field,
  ids and names sanitised, own exponential backoff (2..60 s) with a recreated client when the
  reconnect API refuses.

## Fixed on the strip (local commit on `feat/mqtt-controllers`, not pushed)

1. The link task spun at 100 % while the broker was unreachable and a controller-list change was
   pending: `compute_wait_ms()` returned 0 for the pending flags regardless of the connection. The
   flags now only shorten the wait while connected.
2. An idle strip said nothing for minutes, so the knob's 120 s presence rule greyed it. The
   retained `status` is republished every 60 s while connected (6 bytes), and the wait is capped
   accordingly.
3. A wrong blink code closed the window silently; the knob then sat in "confirming" for 60 s.
   The strip now republishes its unchanged `controllers` document on a rejected code; the knob's
   bridge core, while confirming, reads a list without its id as a failed pairing.

Cost: +160 B of flash (2,006,064 B, 1 % of the slot left). 141 host checks still green; the
pinned IDF 5.4.1 + esp-matter build is clean. Still not run on hardware.

## Left as is

- Only one pairing window at a time; a second controller's request replaces the first one's
  window silently. Fine for a household, worth a red flash on the strip later.
- The strip's app slot is 99 % full; the next feature on the strip needs the partition table or
  the web assets looked at first.
