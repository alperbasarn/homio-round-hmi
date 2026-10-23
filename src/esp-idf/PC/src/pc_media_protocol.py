"""QNOB <-> PC media protocol (PCKNOB-06).

Pure Python: no pywin32, no pycaw, no paho, no tkinter. Everything the desktop app parses or
formats on the MQTT and serial links lives here so the host suite (PC/tests) can pin it on any
machine. The GUI, the audio backend and the broker client only route.

Wire formats (unchanged from the device firmware; see MQTTManager.cpp and SoundController.cpp):

  MQTT payload framing     sender=<client-id>,<body>
  esp32/sound/control      body: play | pause | forward | rewind
  esp32/sound/setpoint     body: setpoint:<0-100>
  esp32/sound/get_state    body: request
  esp32/sound/response     body: playing | paused | state:<playing|paused>,volume:<0-100>
  serial / TCP line        setpoint=<0-100> | playing | paused
"""

from typing import Optional, Tuple

TOPIC_CONTROL = "esp32/sound/control"
TOPIC_SETPOINT = "esp32/sound/setpoint"
TOPIC_RESPONSE = "esp32/sound/response"
TOPIC_GET_STATE = "esp32/sound/get_state"
TOPIC_COMMAND_RESPONSE = "esp32/command/response"

SENDER_PREFIX = "sender="
CONTROL_COMMANDS = ("play", "pause", "forward", "rewind")
VOLUME_MIN = 0
VOLUME_MAX = 100


def split_sender(payload: str) -> Tuple[Optional[str], str]:
    """Split "sender=<id>,<body>" into (sender, body).

    The sender part may sit anywhere in the comma list (the old code searched every part); the
    remaining parts are re-joined in order. A payload without a sender yields (None, payload).
    """
    if not payload:
        return None, ""
    parts = payload.split(",")
    sender = None
    body_parts = []
    for part in parts:
        stripped = part.strip()
        if sender is None and stripped.startswith(SENDER_PREFIX):
            sender = stripped[len(SENDER_PREFIX):]
        else:
            body_parts.append(part)
    return sender, ",".join(body_parts).strip()


def is_self(payload: str, client_id: str) -> bool:
    """True when the payload carries the sender id of this client (an echo from the broker)."""
    sender, _ = split_sender(payload)
    return sender is not None and sender == client_id


def with_sender(client_id: str, message: str) -> str:
    """Frame an outbound message; already-framed messages pass through unchanged."""
    framed_prefix = f"{SENDER_PREFIX}{client_id}"
    if message == framed_prefix or message.startswith(framed_prefix + ","):
        return message
    return f"{framed_prefix},{message}"


def parse_control(body: str) -> Optional[str]:
    """One of CONTROL_COMMANDS, or None for anything else (unknown commands are ignored)."""
    command = body.strip()
    return command if command in CONTROL_COMMANDS else None


def _parse_volume(text: str) -> Optional[int]:
    text = text.strip()
    if not text or len(text) > 4:
        return None
    if not (text.isdigit() or (text[0] in "+-" and text[1:].isdigit())):
        return None
    value = int(text)
    if value < VOLUME_MIN or value > VOLUME_MAX:
        return None
    return value


def parse_setpoint(body: str) -> Optional[int]:
    """"setpoint:<n>" (MQTT) or "setpoint=<n>" (serial), 0..100; None otherwise.

    Out-of-range and non-numeric values are rejected rather than clamped, matching the
    device-side parser (APP-01).
    """
    text = body.strip()
    for separator in (":", "="):
        prefix = f"setpoint{separator}"
        if text.startswith(prefix):
            return _parse_volume(text[len(prefix):].split(",")[0])
    return None


def is_state_request(body: str) -> bool:
    return "request" in body


def format_play_state(is_playing: bool) -> str:
    return "playing" if is_playing else "paused"


def format_state(is_playing: bool, volume: int) -> str:
    """The get_state reply: state:<playing|paused>,volume:<0-100>."""
    clamped = max(VOLUME_MIN, min(VOLUME_MAX, int(volume)))
    return f"state:{format_play_state(is_playing)},volume:{clamped}"


def parse_state(body: str) -> Optional[dict]:
    """Parse a response body into {"playing": bool, "volume": int | None}; None if malformed."""
    text = body.strip()
    if text in ("playing", "paused"):
        return {"playing": text == "playing", "volume": None}
    playing = None
    volume = None
    for part in text.split(","):
        key, sep, value = part.strip().partition(":")
        if not sep:
            return None
        if key == "state":
            if value not in ("playing", "paused"):
                return None
            playing = value == "playing"
        elif key == "volume":
            volume = _parse_volume(value)
            if volume is None:
                return None
        else:
            return None
    if playing is None:
        return None
    return {"playing": playing, "volume": volume}


def parse_serial_line(line: str) -> Optional[tuple]:
    """A line from the device over serial/TCP: ("setpoint", n) | ("state", bool) | None."""
    text = line.strip()
    if text.startswith("setpoint="):
        value = parse_setpoint(text)
        return ("setpoint", value) if value is not None else None
    if text in ("playing", "paused"):
        return ("state", text == "playing")
    return None


def format_serial_setpoint(volume: int) -> str:
    clamped = max(VOLUME_MIN, min(VOLUME_MAX, int(volume)))
    return f"setpoint={clamped}"
