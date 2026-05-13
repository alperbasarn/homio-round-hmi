"""DeviceClient — orchestrates one or two transports per device, picks the active one."""

from __future__ import annotations

import asyncio
import logging
from typing import Any

from qnob_companion.protocol import Response
from qnob_companion.transport.base import Transport, TransportError, TransportState
from qnob_companion.transport.ble import BleTransport
from qnob_companion.transport.tcp import TcpTransport

log = logging.getLogger(__name__)


class DeviceClient:
    """Holds 0..2 transports for a single device and picks the active one.

    Transport preference: TCP > BLE (TCP is faster and OTA-capable). Auto
    fail-over to BLE happens implicitly via ``active_transport`` lookups.

    The pairing token is held here (set via ``set_auth_token``) and injected
    into every outbound envelope.
    """

    def __init__(
        self,
        *,
        tcp: TcpTransport | None = None,
        ble: BleTransport | None = None,
    ) -> None:
        if tcp is None and ble is None:
            raise ValueError("DeviceClient needs at least one transport")
        self._tcp = tcp
        self._ble = ble
        self._auth_token: str | None = None

    # ----- pairing -----

    def set_auth_token(self, token: str | None) -> None:
        self._auth_token = token

    @property
    def has_auth_token(self) -> bool:
        return self._auth_token is not None

    # ----- transport selection -----

    @property
    def active_transport(self) -> Transport | None:
        if self._tcp is not None and self._tcp.state == TransportState.CONNECTED:
            return self._tcp
        if self._ble is not None and self._ble.state == TransportState.CONNECTED:
            return self._ble
        return None

    @property
    def is_connected(self) -> bool:
        return self.active_transport is not None

    # ----- lifecycle -----

    async def connect(self) -> None:
        """Bring up all available transports in parallel.

        Returns once at least one transport reports CONNECTED, or after both
        have failed their first attempt. Background reconnect loops keep
        retrying until ``disconnect()``.
        """
        tasks: list[asyncio.Task[None]] = []
        if self._tcp is not None:
            tasks.append(asyncio.create_task(self._tcp.connect(), name="dc-tcp-connect"))
        if self._ble is not None:
            tasks.append(asyncio.create_task(self._ble.connect(), name="dc-ble-connect"))
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    async def disconnect(self) -> None:
        tasks: list[asyncio.Task[None]] = []
        if self._tcp is not None:
            tasks.append(asyncio.create_task(self._tcp.disconnect(), name="dc-tcp-disconnect"))
        if self._ble is not None:
            tasks.append(asyncio.create_task(self._ble.disconnect(), name="dc-ble-disconnect"))
        if tasks:
            await asyncio.gather(*tasks, return_exceptions=True)

    # ----- command dispatch -----

    async def send_command(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        timeout: float = 10.0,
    ) -> Response:
        active = self.active_transport
        if active is None:
            raise TransportError("no active transport")
        return await active.send(cmd, params=params, auth=self._auth_token, timeout=timeout)
