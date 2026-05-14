"""DeviceClient tests using a mock Transport."""

from __future__ import annotations

import asyncio
from typing import Any

import pytest

from qnob_companion.protocol import Response
from qnob_companion.transport import (
    DeviceClient,
    Transport,
    TransportError,
    TransportState,
)


class MockTransport(Transport):
    """In-memory transport that captures sends and returns canned responses."""

    def __init__(self) -> None:
        super().__init__()
        self.sends: list[dict[str, Any]] = []
        self.canned_response: Response | None = None
        self.raise_on_send: BaseException | None = None

    async def connect(self) -> None:
        self._set_state(TransportState.CONNECTED)

    async def disconnect(self) -> None:
        self._set_state(TransportState.DISCONNECTED)

    async def send(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        auth: str | None = None,
        timeout: float = 10.0,
    ) -> Response:
        self.sends.append({"cmd": cmd, "params": params, "auth": auth})
        if self.raise_on_send is not None:
            raise self.raise_on_send
        if self.canned_response is None:
            return Response(id=1, status="ok")
        return self.canned_response


@pytest.mark.asyncio
async def test_constructor_requires_at_least_one_transport() -> None:
    with pytest.raises(ValueError):
        DeviceClient()


@pytest.mark.asyncio
async def test_send_command_uses_active_transport() -> None:
    tcp = MockTransport()
    client = DeviceClient(tcp=tcp)  # type: ignore[arg-type]
    await client.connect()

    await client.send_command("ping")
    assert tcp.sends == [{"cmd": "ping", "params": None, "auth": None}]


@pytest.mark.asyncio
async def test_auth_token_attached_to_every_send() -> None:
    tcp = MockTransport()
    client = DeviceClient(tcp=tcp)  # type: ignore[arg-type]
    await client.connect()

    client.set_auth_token("secret-token")
    await client.send_command("setBrightness", params={"percent": 75})
    assert tcp.sends[-1]["auth"] == "secret-token"

    client.set_auth_token(None)
    await client.send_command("status")
    assert tcp.sends[-1]["auth"] is None


@pytest.mark.asyncio
async def test_send_raises_when_no_transport_connected() -> None:
    tcp = MockTransport()
    client = DeviceClient(tcp=tcp)  # type: ignore[arg-type]
    # Not connected yet
    with pytest.raises(TransportError):
        await client.send_command("ping")


@pytest.mark.asyncio
async def test_falls_back_to_ble_when_tcp_disconnected() -> None:
    tcp = MockTransport()
    ble = MockTransport()
    client = DeviceClient(tcp=tcp, ble=ble)  # type: ignore[arg-type]

    # Only BLE is connected.
    await ble.connect()
    assert client.active_transport is ble
    await client.send_command("ping")
    assert ble.sends and not tcp.sends

    # TCP comes online — preference flips back.
    await tcp.connect()
    assert client.active_transport is tcp
    await client.send_command("ping")
    assert len(tcp.sends) == 1


@pytest.mark.asyncio
async def test_disconnect_tears_down_all_transports() -> None:
    tcp = MockTransport()
    ble = MockTransport()
    client = DeviceClient(tcp=tcp, ble=ble)  # type: ignore[arg-type]
    await client.connect()
    await client.disconnect()
    assert tcp.state == TransportState.DISCONNECTED
    assert ble.state == TransportState.DISCONNECTED


@pytest.mark.asyncio
async def test_id_generator_skips_zero_and_rolls_over() -> None:
    from qnob_companion.transport.base import IdGenerator

    gen = IdGenerator()
    assert gen.next() == 1
    assert gen.next() == 2
    # Force rollover.
    gen._next = 0xFFFFFFFF
    assert gen.next() == 0xFFFFFFFF
    assert gen.next() == 1  # rolled over, skipped 0


@pytest.mark.asyncio
async def test_async_signal_delivers_to_subscribers() -> None:
    from qnob_companion.transport.base import AsyncSignal

    sig: AsyncSignal[str] = AsyncSignal()
    q1 = sig.subscribe()
    q2 = sig.subscribe()
    sig.emit("hello")
    sig.emit("world")
    assert await asyncio.wait_for(q1.get(), timeout=1.0) == "hello"
    assert await asyncio.wait_for(q1.get(), timeout=1.0) == "world"
    assert await asyncio.wait_for(q2.get(), timeout=1.0) == "hello"
    assert await asyncio.wait_for(q2.get(), timeout=1.0) == "world"
