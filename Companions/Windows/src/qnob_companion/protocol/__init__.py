"""Wire protocol — envelopes, responses, notifications.

Mirrors `docs/protocol.md` in the firmware repo. Keep UI-framework-free so
the integration test harness (#52 D2) can import it standalone.
"""

from qnob_companion.protocol.envelope import (
    Envelope,
    Notification,
    Response,
    decode_frame,
    encode_envelope,
)

__all__ = [
    "Envelope",
    "Notification",
    "Response",
    "decode_frame",
    "encode_envelope",
]
