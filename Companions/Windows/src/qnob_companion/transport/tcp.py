"""TCP transport — newline-delimited JSON envelopes, request/response correlation."""

from __future__ import annotations

import asyncio
import contextlib
import logging
from ipaddress import IPv4Address
from typing import Any

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

# Reconnect backoff schedule (seconds). After exhausting the list, holds at the last value.
_RECONNECT_BACKOFFS = (1.0, 2.0, 4.0, 8.0, 16.0, 30.0)
_HEARTBEAT_INTERVAL_S = 15.0
_CONNECT_TIMEOUT_S = 5.0


class TcpTransport(Transport):
    """Speaks the Qnob TCP wire protocol (docs/protocol.md §2.1).

    Maintains a single open connection. Reconnects with exponential backoff
    when the line drops; heartbeats every 15 s to detect silent failures.
    """

    def __init__(self, host: str | IPv4Address, port: int) -> None:
        super().__init__()
        self._host = str(host)
        self._port = port
        self._reader: asyncio.StreamReader | None = None
        self._writer: asyncio.StreamWriter | None = None
        self._pending: dict[int, asyncio.Future[Response]] = {}
        self._reader_task: asyncio.Task[None] | None = None
        self._heartbeat_task: asyncio.Task[None] | None = None
        self._supervisor_task: asyncio.Task[None] | None = None
        self._stop = asyncio.Event()

    # ----- public lifecycle -----

    async def connect(self) -> None:
        if self._supervisor_task is not None and not self._supervisor_task.done():
            return
        self._stop.clear()
        self._supervisor_task = asyncio.create_task(
            self._supervisor_loop(), name=f"tcp-supervisor-{self._host}:{self._port}"
        )

    async def disconnect(self) -> None:
        self._stop.set()
        await self._teardown_connection()
        if self._supervisor_task is not None:
            self._supervisor_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._supervisor_task
            self._supervisor_task = None
        # Fail any in-flight callers so they unblock.
        for fut in list(self._pending.values()):
            if not fut.done():
                fut.set_exception(TransportError("transport closed"))
        self._pending.clear()
        self._set_state(TransportState.DISCONNECTED)

    async def send(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        auth: str | None = None,
        timeout: float = 10.0,
    ) -> Response:
        if self.state != TransportState.CONNECTED or self._writer is None:
            raise TransportError("transport not connected")

        env = Envelope(id=self._id_gen.next(), cmd=cmd, params=params, auth=auth)
        loop = asyncio.get_running_loop()
        fut: asyncio.Future[Response] = loop.create_future()
        self._pending[env.id] = fut

        line = (encode_envelope(env) + "\n").encode("utf-8")
        try:
            self._writer.write(line)
            await self._writer.drain()
            return await asyncio.wait_for(fut, timeout=timeout)
        finally:
            self._pending.pop(env.id, None)

    # ----- internal: connection supervisor -----

    async def _supervisor_loop(self) -> None:
        """Open the connection; on drop, reconnect with exponential backoff."""
        attempt = 0
        while not self._stop.is_set():
            self._set_state(
                TransportState.RECONNECTING if attempt > 0 else TransportState.CONNECTING
            )
            try:
                self._reader, self._writer = await asyncio.wait_for(
                    asyncio.open_connection(self._host, self._port),
                    timeout=_CONNECT_TIMEOUT_S,
                )
            except (OSError, asyncio.TimeoutError) as exc:
                delay = _RECONNECT_BACKOFFS[min(attempt, len(_RECONNECT_BACKOFFS) - 1)]
                log.info("TCP connect to %s:%d failed (%s); retry in %.0fs",
                         self._host, self._port, exc, delay)
                attempt += 1
                try:
                    await asyncio.wait_for(self._stop.wait(), timeout=delay)
                    return  # disconnect() called during sleep
                except asyncio.TimeoutError:
                    continue

            attempt = 0
            self._set_state(TransportState.CONNECTED)
            log.info("TCP connected to %s:%d", self._host, self._port)

            self._reader_task = asyncio.create_task(self._reader_loop(), name="tcp-reader")
            self._heartbeat_task = asyncio.create_task(self._heartbeat_loop(), name="tcp-heartbeat")

            # Wait for either reader to finish (disconnect) or stop signal.
            stop_wait = asyncio.create_task(self._stop.wait(), name="tcp-stop-wait")
            try:
                await asyncio.wait(
                    {self._reader_task, stop_wait},
                    return_when=asyncio.FIRST_COMPLETED,
                )
            finally:
                if not stop_wait.done():
                    stop_wait.cancel()
                    with contextlib.suppress(asyncio.CancelledError):
                        await stop_wait

            await self._teardown_connection()
            if self._stop.is_set():
                return
            log.info("TCP connection dropped; reconnecting")

    async def _teardown_connection(self) -> None:
        for task_attr in ("_reader_task", "_heartbeat_task"):
            task: asyncio.Task[None] | None = getattr(self, task_attr)
            if task is not None and not task.done():
                task.cancel()
                with contextlib.suppress(asyncio.CancelledError):
                    await task
            setattr(self, task_attr, None)

        if self._writer is not None:
            self._writer.close()
            with contextlib.suppress(Exception):
                await self._writer.wait_closed()
            self._writer = None
        self._reader = None

        # Fail in-flight callers so they unblock instead of timing out.
        for fut in list(self._pending.values()):
            if not fut.done():
                fut.set_exception(TransportError("connection lost"))
        self._pending.clear()

    # ----- internal: per-connection tasks -----

    async def _reader_loop(self) -> None:
        assert self._reader is not None
        try:
            while True:
                line_bytes = await self._reader.readline()
                if not line_bytes:
                    log.debug("TCP reader: EOF")
                    return
                line = line_bytes.decode("utf-8", errors="replace").rstrip("\r\n")
                if not line:
                    continue
                try:
                    frame = decode_frame(line)
                except ValueError as exc:
                    log.warning("Dropped malformed frame: %s", exc)
                    continue
                if isinstance(frame, Notification):
                    await self.notifications.put(frame)
                else:  # Response
                    fut = self._pending.get(frame.id)
                    if fut is not None and not fut.done():
                        fut.set_result(frame)
                    else:
                        log.debug("Response with no pending waiter: id=%d", frame.id)
        except (ConnectionError, asyncio.IncompleteReadError) as exc:
            log.info("TCP reader exited: %s", exc)

    async def _heartbeat_loop(self) -> None:
        try:
            while True:
                await asyncio.sleep(_HEARTBEAT_INTERVAL_S)
                if self.state != TransportState.CONNECTED:
                    continue
                try:
                    await self.send("ping", timeout=5.0)
                except (asyncio.TimeoutError, TransportError) as exc:
                    log.warning("TCP heartbeat failed (%s); dropping connection", exc)
                    # Forcibly close the writer so the reader loop sees EOF and
                    # the supervisor reconnects.
                    if self._writer is not None:
                        self._writer.close()
                    return
        except asyncio.CancelledError:
            raise
