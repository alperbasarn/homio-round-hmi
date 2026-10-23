"""BLE Nordic-UART transport.

Wire protocol (matches BluetoothManager.cpp):
  RX char 0xABF1  client→device  newline-terminated JSON writes (fragmented)
  TX char 0xABF2  device→client  newline-terminated JSON notifications (fragmented)

Framing is identical to TCP — newline-delimited JSON — so the same
decode_frame / encode_envelope helpers are reused.
"""

from __future__ import annotations

import asyncio
import contextlib
import logging
from typing import Any

from bleak import BleakClient
from bleak.exc import BleakError

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

BLE_AVAILABLE = True

_SERVICE_UUID = "0000abf0-0000-1000-8000-00805f9b34fb"
_RX_CHAR_UUID = "0000abf1-0000-1000-8000-00805f9b34fb"  # client writes here
_TX_CHAR_UUID = "0000abf2-0000-1000-8000-00805f9b34fb"  # device notifies here

# Conservative write chunk — keeps each GATT write under typical negotiated MTU.
# The firmware accumulates writes until it sees '\n', so chunking is transparent.
_WRITE_CHUNK = 200

_RECONNECT_BACKOFFS = (1.0, 2.0, 4.0, 8.0, 16.0, 30.0)
_CONNECT_TIMEOUT_S = 10.0


class BleTransport(Transport):
    """Nordic-UART (ABF0/ABF1/ABF2) transport over BLE.

    Reconnects automatically with exponential backoff on connection loss.
    Framing is newline-delimited JSON, identical to the TCP transport.
    Responses are correlated by envelope id, same as TCP.
    """

    def __init__(self, address: str) -> None:
        super().__init__()
        self.address = address
        self._client: BleakClient | None = None
        self._pending: dict[int, asyncio.Future[Response]] = {}
        self._rx_buf = b""
        self._stop = asyncio.Event()
        self._supervisor_task: asyncio.Task[None] | None = None

    # ----- public lifecycle -----

    async def connect(self) -> None:
        if self._supervisor_task is not None and not self._supervisor_task.done():
            return
        self._stop.clear()
        self._supervisor_task = asyncio.create_task(
            self._supervisor_loop(), name=f"ble-supervisor-{self.address}"
        )

    async def disconnect(self) -> None:
        self._stop.set()
        if self._client is not None:
            with contextlib.suppress(Exception):
                await self._client.disconnect()
            self._client = None
        if self._supervisor_task is not None:
            self._supervisor_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._supervisor_task
            self._supervisor_task = None
        self._fail_pending(TransportError("BLE transport closed"))
        self._set_state(TransportState.DISCONNECTED)

    async def send(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        auth: str | None = None,
        timeout: float = 10.0,
    ) -> Response:
        if self.state != TransportState.CONNECTED or self._client is None:
            raise TransportError("BLE not connected")

        env = Envelope(id=self._id_gen.next(), cmd=cmd, params=params, auth=auth)
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[Response] = loop.create_future()
        self._pending[env.id] = fut

        payload = (encode_envelope(env) + "\n").encode("utf-8")
        try:
            await self._write_fragmented(payload)
            return await asyncio.wait_for(fut, timeout=timeout)
        except asyncio.TimeoutError:
            raise
        except (BleakError, OSError) as exc:
            raise TransportError(f"BLE write failed: {exc}") from exc
        finally:
            self._pending.pop(env.id, None)

    # ----- internal: supervisor -----

    async def _supervisor_loop(self) -> None:
        attempt = 0
        while not self._stop.is_set():
            self._set_state(
                TransportState.RECONNECTING if attempt > 0 else TransportState.CONNECTING
            )

            disconnected_event = asyncio.Event()
            loop = asyncio.get_running_loop()

            def _on_disconnected(_client: BleakClient) -> None:
                loop.call_soon_threadsafe(disconnected_event.set)

            try:
                client = BleakClient(
                    self.address,
                    timeout=_CONNECT_TIMEOUT_S,
                    disconnected_callback=_on_disconnected,
                )
                await client.connect()
                await client.start_notify(_TX_CHAR_UUID, self._on_notification)
                self._client = client
                attempt = 0
                self._set_state(TransportState.CONNECTED)
                log.info("BLE connected to %s", self.address)

                # Wait until disconnect or stop signal
                stop_task = asyncio.create_task(self._stop.wait(), name="ble-stop-wait")
                disc_task = asyncio.create_task(disconnected_event.wait(), name="ble-disc-wait")
                try:
                    await asyncio.wait(
                        {stop_task, disc_task}, return_when=asyncio.FIRST_COMPLETED
                    )
                finally:
                    for t in (stop_task, disc_task):
                        if not t.done():
                            t.cancel()
                            with contextlib.suppress(asyncio.CancelledError):
                                await t

                self._client = None
                self._fail_pending(TransportError("BLE connection lost"))

                if not self._stop.is_set():
                    # Peer-initiated disconnect — clean up and reconnect
                    log.info("BLE disconnected from %s; reconnecting", self.address)
                    with contextlib.suppress(Exception):
                        await client.disconnect()
                    self._set_state(TransportState.RECONNECTING)
                    continue

                with contextlib.suppress(Exception):
                    await client.disconnect()
                return

            except (BleakError, asyncio.TimeoutError, OSError) as exc:
                delay = _RECONNECT_BACKOFFS[min(attempt, len(_RECONNECT_BACKOFFS) - 1)]
                log.info(
                    "BLE connect to %s failed (%s); retry in %.0f s",
                    self.address, exc, delay,
                )
                attempt += 1
                try:
                    await asyncio.wait_for(self._stop.wait(), timeout=delay)
                    return  # disconnect() called during backoff sleep
                except asyncio.TimeoutError:
                    continue

        self._set_state(TransportState.DISCONNECTED)

    # ----- internal: write -----

    async def _write_fragmented(self, data: bytes) -> None:
        assert self._client is not None
        for i in range(0, len(data), _WRITE_CHUNK):
            chunk = data[i : i + _WRITE_CHUNK]
            await self._client.write_gatt_char(_RX_CHAR_UUID, chunk, response=False)

    # ----- internal: notification handler -----

    def _on_notification(self, _handle: int, data: bytes) -> None:
        self._rx_buf += data
        while b"\n" in self._rx_buf:
            line_bytes, self._rx_buf = self._rx_buf.split(b"\n", 1)
            line = line_bytes.decode("utf-8", errors="replace").strip()
            if not line:
                continue
            try:
                frame = decode_frame(line)
            except ValueError as exc:
                log.warning("BLE: malformed frame: %s", exc)
                continue
            if isinstance(frame, Notification):
                self.notifications.put_nowait(frame)
            else:
                fut = self._pending.get(frame.id)
                if fut is not None and not fut.done():
                    fut.set_result(frame)
                else:
                    log.debug("BLE: response with no pending waiter: id=%d", frame.id)

    # ----- internal: helpers -----

    def _fail_pending(self, exc: Exception) -> None:
        for fut in list(self._pending.values()):
            if not fut.done():
                fut.set_exception(exc)
        self._pending.clear()
