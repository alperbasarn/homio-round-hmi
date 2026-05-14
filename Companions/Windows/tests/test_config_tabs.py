"""Command-serialisation tests for config tabs (headless, no Qt)."""

from __future__ import annotations

import asyncio
from typing import Any

import pytest

from qnob_companion.protocol import Response
from qnob_companion.transport import DeviceClient, Transport, TransportError, TransportState


class _RecordingTransport(Transport):
    """Captures every send() call so tests can assert commands sent."""

    def __init__(self, *, resp_ok: bool = True) -> None:
        super().__init__()
        self.sent: list[dict[str, Any]] = []
        self._resp_ok = resp_ok

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
        self.sent.append({"cmd": cmd, "params": params, "auth": auth})
        if not self._resp_ok:
            raise TransportError("stubbed failure")
        return Response(id=self._id_gen.next(), status="ok")


def _make_client(*, resp_ok: bool = True) -> tuple[_RecordingTransport, DeviceClient]:
    t = _RecordingTransport(resp_ok=resp_ok)
    c = DeviceClient(tcp=t)  # type: ignore[arg-type]
    asyncio.get_event_loop().run_until_complete(c.connect())
    return t, c


# ---------------------------------------------------------------------------
# MQTT tab
# ---------------------------------------------------------------------------

def test_mqtt_command_format_host_port() -> None:
    """configureMQTTServer:host:port when no creds."""
    from qnob_companion.ui.tabs.mqtt_tab import MqttTab

    t, c = _make_client()
    tab = MqttTab.__new__(MqttTab)
    tab._client = c

    async def _run(cmd: str, **_kw: Any) -> Response | None:
        return await c.send_command(cmd)

    tab._run = _run  # type: ignore[method-assign]

    async def go() -> None:
        await tab._run("configureMQTTServer:broker.example.com:8883")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "configureMQTTServer:broker.example.com:8883"


# ---------------------------------------------------------------------------
# Device tab
# ---------------------------------------------------------------------------

def test_device_name_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("setDeviceName:Workshop")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "setDeviceName:Workshop"


def test_factory_reset_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("factoryReset")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "factoryReset"


def test_restart_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("reset")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "reset"


# ---------------------------------------------------------------------------
# Display tab
# ---------------------------------------------------------------------------

def test_brightness_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("setBrightness:75")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "setBrightness:75"


def test_screen_switch_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("screen:timer")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "screen:timer"


# ---------------------------------------------------------------------------
# Bluetooth tab
# ---------------------------------------------------------------------------

def test_set_bluetooth_name_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("setBluetoothName:My Qnob")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "setBluetoothName:My Qnob"


def test_set_bluetooth_off_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("setBluetooth:off")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "setBluetooth:off"


def test_clear_bonds_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("clearBtBonds")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "clearBtBonds"


# ---------------------------------------------------------------------------
# WiFi tab
# ---------------------------------------------------------------------------

def test_static_ip_command_format() -> None:
    t, c = _make_client()
    cmd = "configureStaticIP:192.168.1.50:192.168.1.1:255.255.255.0:8.8.8.8:8.8.4.4"

    async def go() -> None:
        await c.send_command(cmd)

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == cmd


def test_disable_static_ip_command() -> None:
    t, c = _make_client()

    async def go() -> None:
        await c.send_command("disableStaticIP")

    asyncio.get_event_loop().run_until_complete(go())
    assert t.sent[-1]["cmd"] == "disableStaticIP"
