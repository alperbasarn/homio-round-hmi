# Bench runbook (1.75 board, state of 2026-09-13)

One page for the next bench session. Everything below runs from Windows Git Bash through WSL:

    wsl -u buildserver -- bash -lc 'bash /mnt/c/Users/alper/Documents/GitHub/QNOB/screen/SW/ESP/tools/bench/<script>.sh'

Logs land in `Hardware Ref/bench_logs/` (ignored by git); the WiFi PSK is scrubbed from them.
Build first: `bash build-variant.sh esp32s3_amoled175` in WSL with `sdkconfig.defaults.local`
carrying `CONFIG_QNOB_MODEL_TRACE=y`, `CONFIG_QNOB_DEEP_SLEEP=n`, `CONFIG_QNOB_LIGHT_SLEEP=n`.

## 0. Setup (once per session)

1. Plug the board; `usbipd list` must show `303a:1001` on bus 4-4. If it is not attached, run
   `usbipd attach --wsl --busid 4-4 --auto-attach` (the bind was done as admin earlier).
   WSL shuts down 8 s after the last process, which drops the attach; the helper re-attaches.
2. A tap does not wake a deep-sleeping board and the bench build has deep sleep off. If the board
   is missing from the bus, press RST, do not re-plug, and go to step 3 of the drop protocol.

## 1. Knob alone

| Step | Script | Pass when the summary shows |
|---|---|---|
| A1 boot | `boot_check.sh` | `Boot screen up, display task started` before the touch and WiFi lines; `Boot handoff to INFO` about 2.9 s after it; `Got IP address` within 5 s; watchdog 0, panic 0, lvgl 0; every drain `offtask=0`; a `Black box:` line and a `PMU at boot:` line; `sysInfo:` answered |
| A2 WP3.1, eyes | during A1 | the arc fills through the whole boot (no jump, no second ramp), then INFO |
| A3 gesture walk | `capture.sh 120 <log>` then use the panel | one `input` line per gesture; a swipe gives a gesture line and no release line (W3); INFO <-> DEVICE_INFO round trip; wheel -> LIGHT shows "no strip" and a 3 s hold returns to INFO; a tap in SLEEP wakes and is consumed |
| A4 sleep gate | `sleep_check.sh` | `Stage 3 skipped` at 180 s, heartbeats through 240 s, port still present |
| A5 MEDIA offline | during A3 | wheel -> MEDIA (double-tap SOUND) shows the ring at the last volume, `Syncing...`, then `No broker` after 3 s; the knob moves the ring with one `input` line per detent and no publish; a tap flashes `Not available` for a second; a double-tap flashes it too (no duration known); a 3 s hold returns to INFO; the DEVICE_INFO sound bar shows the same volume |
| A6 DEVICE_INFO | during A3 | swipe up from INFO: the page appears at once with no dip and no banded repaint; the signal glyph lights the dot plus one arc per bar (four levels, not three); the battery icon is grey with no fill on this battery-less board; dragging the left bar dims and brightens the panel; dragging the right bar moves the volume and the MEDIA ring agrees; a tap on the bottom band returns to INFO; the right edge also returns to INFO; the left edge does nothing; pulling the AP greys the glyph within a second with no touch |
| A7 knob write (needs the V11 knob on GPIO 16/17) | after A5 | entering MEDIA or LIGHT writes one `setpoint=N` line to the knob (`Updated setpoint:` in the knob log) and the next detent continues from N; with no knob attached the model adopts the value after 1 s and `input knob-ack` never appears |

## 2. USB-drop protocol (the second cause is still open)

1. After A1, leave the board idle with the port released (no capture running) and note the time.
2. When it vanishes from `usbipd list`, press RST (not re-plug), wait for the port to come back.
3. Run `diag.sh <log> 60 --no-flash`. Read the `sysInfo:` block:
   - `previous run uptime=N ... (chip kept power)` means the chip was alive when USB died: a USB
     or PLL state problem, and `off: none` from the PMU. Note N against the idle time.
   - `previous run: no record (first boot, or power was lost)` means the chip lost power. Then
     `PMU at boot: ... off:` names the reason: `vsys_uv` (VBUS sag with no battery), `overtemp`,
     `pwron_low`, `sw`, or `src_pulldown` (VBUS removed).
4. Then `diag.sh <log> 900` holds the port 15 minutes. No drop while held and a drop within
   minutes when released points at the host side (usbipd detach cycles, selective suspend).

## 3. Knob and strip

1. Broker on the knob (console, then `reset`): `configureMQTTServer:<host>:<port>[:<user>:<pass>]`;
   the boot then shows `MQTT connected` and `LED bridge up: controller id qnob-xxxxxx`.
2. Strip on the `dev` branch build (esp32c6-led-web): flash, commission, open its SoftAP page,
   Configuration -> MQTT Link, same broker. Watch `homio/led/#` with any MQTT client.
3. `led_check.sh <log> 120`: `leds:` lists the strip as `online available` (retained info and
   status replayed), `ledStatus:` shows `dropped=0`.
4. Pair from the console: `led_check.sh <log> 120 --send "ledPair:0@10"`. A factory-fresh strip
   pairs at once (`leds:` says `mine`, the strip flashes green). Otherwise count the blinks and add
   `--send "ledCode:N@40"`. A wrong code republishes `controllers` and `ledStatus` reads
   `pairing=failed`.
5. LIGHT screen: wheel -> LIGHT shows the strip name with a green badge. Tap: the strip toggles.
   Knob: brightness follows, the MQTT client shows one `set` per 100 ms at most during a drag and
   a `state` echo after each. Swipe: the effect name cycles. Ring drag: colour follows the angle.
   3 s hold: back to INFO. `ledStatus:` must show `failed=0`.
6. Web page change on the strip: the knob follows within a second, no `set` published.
7. Unplug the strip: the badge greys within 45 s; a knob turn is refused locally (no `set`).
   Plug it back: `online` again within its reconnect backoff.
8. Two strips (LED-08): both listed, both pair, the rim shows two segments, a rim tap switches,
   each strip follows its own controls.
9. PC media (MEDIA-16, needs the PC app on the same broker): wheel -> MEDIA syncs within a
   second (`Playing`/`Paused` and the PC volume). Tap: the PC toggles, the label follows the
   reply. Knob: the PC volume follows with at most one `setpoint:` per 120 ms on the MQTT
   client, and the ring never jumps back when the echo arrives. Two quick taps: one toggle
   only (the second answers `Busy` in the log, never a double-toggle). Swipe left/right: next
   and previous track. DEVICE_INFO: dragging the sound bar moves the PC volume. Stop the PC
   app: `PC not answering` after the next request, the knob still moves the ring.

## 4. After the session

Summarise each log in the phase logs (phase 2 for the drop protocol, phase 3 for A2, A3 and
the LED path) and commit; the `p0-fixed` screenshots and the push of the strip branch stay owner
items.
