# Knob link protocol (PCKNOB-04, D5, D6)

The V11 knob (`Motor Driver/HapticController3`, an ESP32-C3 running SimpleFOC) and the screen
talk over one UART at 9600 8N1 (`serial.ino:56`, `KnobController.cpp:28-50`). This is the
contract for that link: what exists today (v1), what each token means, who produces and who
consumes it, and the v2 form that Phase 2 lands (screen-side parser first, knob-side emitter
second, so no window ever drops detents).

## Authority (D5)

The screen is authoritative for every mode value (volume, brightness, climate target). The knob
holds a replica counter (`setPoint`, `HapticController3.ino:14`, clamped 0..100 in
`FOC.ino`) that only exists so the haptic loop knows where the detents are. On the screen the
counter is `KnobModel::detentCount`; a `+N`/`-N` line is a delta, a coalesced `setpoint=N` line
becomes `N - lastN` clamped to +-10 and is never written into a mode value
(`KnobModel::burstToDelta`). Entering a mode pushes the mode's value down to the knob
(`KnobModel::onEnter` -> `setpoint=N`), and the replica follows the ack or a 1 s timeout.

## Framing

| | v1 (today) | v2 |
|---|---|---|
| Terminator | none: `Serial1.print(toSend)` (`FOC.ino:151,162`) | every line ends `\n` (0x0A) |
| Screen reader | reads whatever is buffered, keeps the bytes before the first CR/LF, drops the rest (`KnobController.cpp:80-100`) | accumulating line assembler (PCKNOB-09), legacy bare `+`/`-` still accepted |
| Failure mode | two detents in one 100 ms read arrive as `++`, which matches nothing: every detent past the first per read is lost | none |

## Tokens, knob -> screen

| Token | v1 producer | v1 consumer(s) | Meaning | v2 |
|---|---|---|---|---|
| `+` | `FOC.ino:148-151` (one per loop when `incrementToSend == 1`) | `DisplayController.cpp:199,222,245` per mode, and again `CommandHandler.cpp:123-129` (`commands["+"]`) from the command task: two consumers per detent (CTRL-01, INPT-21 removes one) | one detent clockwise | `+N\n`, N >= 1, the number of detents accumulated since the last line |
| `-` | `FOC.ino:159-162` | same two | one detent counter-clockwise | `-N\n` |
| `setpoint=N` | `FOC.ino:145,156` when more than one detent accumulated in one loop | dropped: `CommandHandler.cpp` splits on `:` only, so `setpoint=N` matches no command; DisplayController ignores it | the knob's replica counter after a burst | never a value: `KnobModel::burstToDelta` (WP1.3) turns it into a clamped delta; INPT-05 posts it as `KnobModel::Field::Burst` |
| `calibrate` | `serial.ino:39-40` forwards the USB console word to the screen | `CommandHandler.cpp` `calibrateOrientation` (a logged no-op since APP-03) | debug only | dropped |
| `pong:<ms>` | not implemented | not implemented | liveness reply | PCKNOB-17: `KnobModel::noteLinkActivity` |
| `ack:setpoint=N` | not implemented; the knob prints `Updated setpoint: N` to **USB** `Serial` (`serial.ino:30-31`), never to the link | none | write acknowledgement | PCKNOB-15: the ack goes to `Serial1`; `KnobModel::noteWriteAck` |

## Tokens, screen -> knob

| Token | v1 producer | v1 consumer | Meaning | v2 |
|---|---|---|---|---|
| `setpoint=N` | `KnobController::sendCommand` (`KnobController.cpp:112-126`) has **no caller** outside the driver; the write path is dead | `serial.ino:28-31` (USB console only: `updateSerial()` reads `Serial`, not `Serial1`; the link listener `updateSoftSerial()` handles what it handles, see `serial.ino:60+`) | set the replica counter | `setpoint=N\n` on the link, acked (PCKNOB-15) |
| `sp?` | `KnobController::askSetpoint` (`KnobController.cpp:66-69`), no caller | no handler on the knob (`serial.ino` falls to the usage line) | query the counter | dropped: the screen is authoritative |
| `poles=N`, `strength=F`, `snappoint=F`, `voltagelimit=F` | none on the link | `serial.ino:6-27` (USB console) | haptic config (`KnobModel::HapticConfig`, defaults 120 / 1.0 / 0.55 / 3.0) | same words on the link, LF-terminated; `poles` stays until section 9 Q32 is answered (it is detents per revolution) |
| `ping` | not implemented | not implemented | liveness | PCKNOB-17 |
| `enable`, `disable`, `calibrate`, `status` | USB console only | `serial.ino:32-48` | bench verbs | stay USB-only |

## Separators

`=` is the assignment separator of the knob link (`setpoint=N`, `poles=N`). `:` belongs to the
screen's command grammar over serial, TCP and MQTT (`setpoint:N`, `configureMQTTServer:...`) and
never appears on the knob link. The two must not be normalised into one another: a `setpoint=N`
that reaches `cmdKnobSetpoint` is the cross-mode leak the plan describes (INPT-22 retires that
path; `KnobModel` converts bursts to deltas instead).

## Producer / consumer count (the PCKNOB-04 check)

Every v2 token has exactly one producer and one consumer: knob motion lines are produced by
`handleIncrementDecrement` and consumed by the screen-side line assembler feeding
`KnobModel::applyDelta` / `burstToDelta`; `setpoint=`/config writes are produced by
`KnobController::sendCommand` on request of `KnobModel::takeWriteRequest` and consumed by the
knob's link listener; `ping`/`pong` pair the two. The v1 double consumption of `+`/`-` is the
defect INPT-21 removes.
