"""TcpTransport tests against a loopback fake server."""

from __future__ import annotations

import asyncio
import json
from collections.abc import Callable
from contextlib import asynccontextmanager
from typing import Any

import pytest

from qnob_companion.transport import (
    TcpTransport,
    TransportError,
    TransportState,
)

# ---------------------------------------------------------------------------
# Fake server — handles one client, runs an envelope handler per inbound line.
# ---------------------------------------------------------------------------

EnvelopeHandler = Callable[[dict[str, Any]], dict[str, Any] | None]


@asynccontextmanager
async def fake_server(handler: EnvelopeHandler):
    """Start a localhost TCP server that echoes handler responses line-by-line.

    Yields ``(host, port)`` for client connection.
    """
    connections: list[asyncio.StreamWriter] = []

    async def on_client(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        connections.append(writer)
        try:
            while True:
                line = await reader.readline()
                if not line:
                    return
                try:
                    env = json.loads(line.decode().rstrip("\n"))
                except json.JSONDecodeError:
                    continue
                resp = handler(env)
                if resp is not None:
                    writer.write((json.dumps(resp) + "\n").encode())
                    await writer.drain()
        except (ConnectionError, asyncio.CancelledError):
            pass

    server = await asyncio.start_server(on_client, "127.0.0.1", 0)
    addr = server.sockets[0].getsockname()
    try:
        yield addr[0], addr[1]
    finally:
        for w in connections:
            w.close()
        server.close()
        await server.wait_closed()


async def _wait_for_state(transport: TcpTransport, state: TransportState, timeout: float = 2.0) -> None:
    deadline = asyncio.get_running_loop().time() + timeout
    while transport.state != state:
        if asyncio.get_running_loop().time() > deadline:
            raise asyncio.TimeoutError(f"timeout waiting for {state}, got {transport.state}")
        await asyncio.sleep(0.02)


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------


@pytest.mark.asyncio
async def test_ping_round_trip() -> None:
    def handler(env: dict[str, Any]) -> dict[str, Any]:
        assert env["cmd"] == "ping"
        return {"id": env["id"], "status": "ok", "data": {"uptime_ms": 12345}}

    async with fake_server(handler) as (host, port):
        tcp = TcpTransport(host, port)
        try:
            await tcp.connect()
            await _wait_for_state(tcp, TransportState.CONNECTED)
            resp = await tcp.send("ping")
            assert resp.is_ok
            assert resp.data == {"uptime_ms": 12345}
        finally:
            await tcp.disconnect()


@pytest.mark.asyncio
async def test_concurrent_sends_correlate_by_id() -> None:
    """Two parallel sends must each receive their own response."""

    def handler(env: dict[str, Any]) -> dict[str, Any]:
        return {"id": env["id"], "status": "ok", "data": {"echo": env["cmd"]}}

    async with fake_server(handler) as (host, port):
        tcp = TcpTransport(host, port)
        try:
            await tcp.connect()
            await _wait_for_state(tcp, TransportState.CONNECTED)
            results = await asyncio.gather(
                tcp.send("alpha"),
                tcp.send("beta"),
                tcp.send("gamma"),
            )
            assert {r.data["echo"] for r in results if r.data} == {"alpha", "beta", "gamma"}
        finally:
            await tcp.disconnect()


@pytest.mark.asyncio
async def test_error_response_is_returned() -> None:
    def handler(env: dict[str, Any]) -> dict[str, Any]:
        return {"id": env["id"], "status": "err", "error": "bad_params", "detail": "out of range"}

    async with fake_server(handler) as (host, port):
        tcp = TcpTransport(host, port)
        try:
            await tcp.connect()
            await _wait_for_state(tcp, TransportState.CONNECTED)
            resp = await tcp.send("setBrightness", params={"percent": 999})
            assert not resp.is_ok
            assert resp.error == "bad_params"
            assert resp.detail == "out of range"
        finally:
            await tcp.disconnect()


@pytest.mark.asyncio
async def test_send_timeout_raises() -> None:
    """Server that never replies → caller times out."""

    def handler(env: dict[str, Any]) -> dict[str, Any] | None:
        return None  # silence

    async with fake_server(handler) as (host, port):
        tcp = TcpTransport(host, port)
        try:
            await tcp.connect()
            await _wait_for_state(tcp, TransportState.CONNECTED)
            with pytest.raises(asyncio.TimeoutError):
                await tcp.send("ping", timeout=0.3)
        finally:
            await tcp.disconnect()


@pytest.mark.asyncio
async def test_disconnect_unblocks_pending_sends() -> None:
    """In-flight callers should fail with TransportError when disconnect() is called."""

    server_started = asyncio.Event()

    def handler(env: dict[str, Any]) -> dict[str, Any] | None:
        server_started.set()
        return None  # never reply

    async with fake_server(handler) as (host, port):
        tcp = TcpTransport(host, port)
        try:
            await tcp.connect()
            await _wait_for_state(tcp, TransportState.CONNECTED)

            send_task = asyncio.create_task(tcp.send("ping", timeout=10.0))
            await asyncio.wait_for(server_started.wait(), timeout=1.0)
            await tcp.disconnect()

            with pytest.raises(TransportError):
                await send_task
        finally:
            if tcp.state != TransportState.DISCONNECTED:
                await tcp.disconnect()


@pytest.mark.asyncio
async def test_send_when_disconnected_raises() -> None:
    tcp = TcpTransport("127.0.0.1", 1)  # no server
    with pytest.raises(TransportError):
        await tcp.send("ping")
