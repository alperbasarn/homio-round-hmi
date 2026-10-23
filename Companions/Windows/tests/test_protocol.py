"""Round-trip tests for the envelope codec."""

from __future__ import annotations

import json

import pytest

from qnob_companion.protocol import (
    Envelope,
    Notification,
    Response,
    decode_frame,
    encode_envelope,
)


def test_envelope_encode_minimal() -> None:
    env = Envelope(id=1, cmd="ping")
    assert encode_envelope(env) == '{"id":1,"cmd":"ping"}'


def test_envelope_encode_with_params_and_auth() -> None:
    env = Envelope(id=42, cmd="setBrightness", params={"percent": 75}, auth="abc")
    obj = json.loads(encode_envelope(env))
    assert obj == {"id": 42, "cmd": "setBrightness", "params": {"percent": 75}, "auth": "abc"}


def test_envelope_id_must_be_uint32() -> None:
    with pytest.raises(ValueError):
        Envelope(id=0, cmd="ping")
    with pytest.raises(ValueError):
        Envelope(id=0x1_0000_0000, cmd="ping")


def test_decode_response_ok() -> None:
    frame = decode_frame('{"id":1,"status":"ok","data":{"uptime_ms":1234}}')
    assert isinstance(frame, Response)
    assert frame.is_ok
    assert frame.data == {"uptime_ms": 1234}


def test_decode_response_err() -> None:
    frame = decode_frame('{"id":2,"status":"err","error":"bad_params","detail":"out of range"}')
    assert isinstance(frame, Response)
    assert not frame.is_ok
    assert frame.error == "bad_params"
    assert frame.detail == "out of range"


def test_decode_notification() -> None:
    frame = decode_frame('{"type":"notification","event":"log","data":{"level":"info","msg":"hi"}}')
    assert isinstance(frame, Notification)
    assert frame.event == "log"


def test_decode_invalid_json_raises() -> None:
    with pytest.raises(ValueError):
        decode_frame("not json{")


def test_decode_non_object_raises() -> None:
    with pytest.raises(ValueError):
        decode_frame("[1,2,3]")
