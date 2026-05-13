"""BLE Nordic-UART transport — STUB.

Implementation lands in the BLE half of #62 B3 (a follow-up PR). This module
exists so callers can import ``BleTransport`` and check ``BLE_AVAILABLE`` to
gate behaviour during the TCP-only Phase 1.
"""

from __future__ import annotations

from typing import Any

from qnob_companion.protocol import Response
from qnob_companion.transport.base import Transport, TransportError

BLE_AVAILABLE = False  # flips to True when the BLE half lands


class BleTransport(Transport):
    """Placeholder. Construction succeeds so the import path stays stable;
    every method raises until implementation lands."""

    def __init__(self, address: str) -> None:
        super().__init__()
        self.address = address

    async def connect(self) -> None:
        raise TransportError("BLE transport not yet implemented (see #62 B3 BLE half)")

    async def disconnect(self) -> None:
        return

    async def send(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        auth: str | None = None,
        timeout: float = 10.0,
    ) -> Response:
        raise TransportError("BLE transport not yet implemented (see #62 B3 BLE half)")
