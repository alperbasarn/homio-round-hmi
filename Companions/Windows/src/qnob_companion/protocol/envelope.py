"""Envelope dataclasses for the Qnob wire protocol (see docs/protocol.md)."""

from __future__ import annotations

import json
from typing import Any, Final, Literal

from pydantic import BaseModel, ConfigDict, Field

# Protocol version this client speaks. Refuse to talk to devices announcing a
# higher major version (per docs/protocol.md §9).
PROTO_VERSION: Final[int] = 1


class Envelope(BaseModel):
    """Client → device request envelope."""

    model_config = ConfigDict(extra="forbid")

    id: int = Field(ge=1, le=0xFFFFFFFF, description="Per-session monotonic id")
    cmd: str
    params: dict[str, Any] | None = None
    auth: str | None = None


class Response(BaseModel):
    """Device → client response envelope."""

    model_config = ConfigDict(extra="forbid")

    id: int = Field(ge=0, le=0xFFFFFFFF)
    status: Literal["ok", "err"]
    data: dict[str, Any] | None = None
    error: str | None = None
    detail: str | None = None

    @property
    def is_ok(self) -> bool:
        return self.status == "ok"


class Notification(BaseModel):
    """Device → client unsolicited push (no id, no response expected)."""

    model_config = ConfigDict(extra="forbid")

    type: Literal["notification"] = "notification"
    event: str
    data: dict[str, Any] | None = None


def encode_envelope(env: Envelope) -> str:
    """Serialise an envelope to its newline-free JSON wire form."""
    payload = env.model_dump(exclude_none=True)
    return json.dumps(payload, separators=(",", ":"))


def decode_frame(line: str) -> Response | Notification:
    """Parse a single inbound frame from the device.

    Raises ``ValueError`` on malformed JSON. Distinguishes notifications
    (have a ``type`` field) from responses (have ``status``).
    """
    try:
        obj = json.loads(line)
    except json.JSONDecodeError as exc:
        raise ValueError(f"invalid JSON frame: {exc}") from exc

    if not isinstance(obj, dict):
        raise ValueError("frame is not a JSON object")

    if obj.get("type") == "notification":
        return Notification.model_validate(obj)

    return Response.model_validate(obj)
