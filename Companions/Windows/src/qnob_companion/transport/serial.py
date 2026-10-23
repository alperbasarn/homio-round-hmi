"""Serial (USB-CDC) transport — newline-delimited JSON envelopes over a COM port.

Unlike TCP/BLE there is no reconnect supervisor; the port is either open or
not.  All raw device output (including ESP_LOG lines) is pushed into the
``raw_lines`` queue so a serial terminal widget can display it.
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
from typing import Any

try:
    import serial_asyncio  # type: ignore[import-untyped]
    SERIAL_AVAILABLE = True
except ImportError:  # pragma: no cover
    SERIAL_AVAILABLE = False

from qnob_companion.protocol import (
    Envelope,
    Notification,
    Response,
    decode_frame,
    encode_envelope,
)
from qnob_companion.transport.base import (
    Transport,
    TransportError,
    TransportState,
)

log = logging.getLogger(__name__)

_CONNECT_TIMEOUT_S = 5.0
DEFAULT_BAUDRATE = 115200


def list_serial_ports() -> list[str]:
    """Return a sorted list of available serial port names (e.g. ``['COM3', 'COM5']``).

    Returns an empty list when ``pyserial`` is not installed.
    """
    try:
        from serial.tools.list_ports import comports  # type: ignore[import-untyped]
        return sorted(p.device for p in comports())
    except ImportError:  # pragma: no cover
        return []


class SerialTransport(Transport):
    """Speaks the Qnob wire protocol over a USB serial (CDC) port.

    After ``connect()`` all inbound lines are available in ``raw_lines``
    (max 2000 entries, oldest dropped on overflow).  JSON responses are also
    correlated to pending ``send()`` calls automatically.
    """

    def __init__(self, port: str, baudrate: int = DEFAULT_BAUDRATE) -> None:
        super().__init__()
        if not SERIAL_AVAILABLE:
            raise ImportError(
                "pyserial-asyncio is not installed. "
                "Install it with: pip install pyserial-asyncio"
            )
        self._port = port
        self._baudrate = baudrate
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._pending: dict[int, asyncio.Future[Response]] = {}
        self._reader_task: asyncio.Task[None] | None = None
        self.raw_lines: asyncio.Queue[str] = asyncio.Queue(maxsize=2000)

    @property
    def port(self) -> str:
        return self._port

    # ----- public lifecycle -----

    async def connect(self) -> None:
        if self.state == TransportState.CONNECTED:
            return
        self._set_state(TransportState.CONNECTING)
        try:
            self._reader, self._writer = await asyncio.wait_for(
                serial_asyncio.open_serial_connection(
                    url=self._port, baudrate=self._baudrate
                ),
                timeout=_CONNECT_TIMEOUT_S,
            )
        except (OSError, asyncio.TimeoutError) as exc:
            self._set_state(TransportState.DISCONNECTED)
            raise TransportError(f"cannot open {self._port}: {exc}") from exc

        self._reader_task = asyncio.create_task(
            self._reader_loop(), name=f"serial-reader-{self._port}"
        )
        self._set_state(TransportState.CONNECTED)
        log.info("Serial port %s opened at %d baud", self._port, self._baudrate)

    async def disconnect(self) -> None:
        if self.state == TransportState.DISCONNECTED:
            return
        self._set_state(TransportState.DISCONNECTED)
        if self._reader_task is not None:
            self._reader_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._reader_task
            self._reader_task = None
        if self._writer is not None:
            with contextlib.suppress(Exception):
                self._writer.close()
                await self._writer.wait_closed()
            self._writer = None
        self._reader = None
        for fut in list(self._pending.values()):
            if not fut.done():
                fut.set_exception(TransportError("serial port closed"))
        self._pending.clear()
        log.info("Serial port %s closed", self._port)

    async def send(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        auth: str | None = None,
        timeout: float = 10.0,
    ) -> Response:
        if self.state != TransportState.CONNECTED or self._writer is None:
            raise TransportError("serial port not connected")

        env = Envelope(id=self._id_gen.next(), cmd=cmd, params=params, auth=auth)
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[Response] = loop.create_future()
        self._pending[env.id] = fut

        line = (encode_envelope(env) + "\n").encode("utf-8")
        try:
            self._writer.write(line)
            await self._writer.drain()
            return await asyncio.wait_for(asyncio.shield(fut), timeout=timeout)
        except asyncio.TimeoutError:
            raise
        finally:
            self._pending.pop(env.id, None)

    async def write_raw(self, text: str) -> None:
        """Write a raw text line to the port (no JSON framing). For terminal use."""
        if self._writer is None:
            raise TransportError("serial port not connected")
        self._writer.write((text.rstrip("\r\n") + "\n").encode("utf-8"))
        await self._writer.drain()

    # ----- internal: reader loop -----

    async def _reader_loop(self) -> None:
        assert self._reader is not None
        try:
            while True:
                raw = await self._reader.readline()
                if not raw:
                    break  # EOF — port closed or device reset
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue

                # Always push every line to raw_lines (for the terminal UI).
                if self.raw_lines.full():
                    try:
                        self.raw_lines.get_nowait()  # drop oldest
                    except asyncio.QueueEmpty:
                        pass
                self.raw_lines.put_nowait(line)

                # Try to parse JSON — route to pending future or notifications.
                if line.startswith("{"):
                    try:
                        frame = decode_frame(line)
                    except (ValueError, Exception):
                        pass
                    else:
                        if isinstance(frame, Response):
                            fut = self._pending.get(frame.id)
                            if fut is not None and not fut.done():
                                fut.set_result(frame)
                        elif isinstance(frame, Notification):
                            with contextlib.suppress(asyncio.QueueFull):
                                self.notifications.put_nowait(frame)

        except asyncio.CancelledError:
            raise
        except Exception as exc:
            log.warning("Serial reader error on %s: %s", self._port, exc)
        finally:
            if self.state == TransportState.CONNECTED:
                self._set_state(TransportState.DISCONNECTED)
            for fut in list(self._pending.values()):
                if not fut.done():
                    fut.set_exception(TransportError("serial port disconnected"))
            self._pending.clear()
