"""DiscoveryService merge + debounce + stale-cleanup tests."""

from __future__ import annotations

import asyncio
from collections.abc import AsyncGenerator
from datetime import datetime, timedelta, timezone

import pytest
import pytest_asyncio

from qnob_companion.discovery import (
    Added,
    DeviceDescriptor,
    DiscoveryService,
    Removed,
    Updated,
)


@pytest_asyncio.fixture
async def service() -> AsyncGenerator[DiscoveryService, None]:
    """Service with mDNS and BLE disabled — we feed _on_partial directly."""
    svc = DiscoveryService(enable_mdns=False, enable_ble=False)
    await svc.start()
    try:
        yield svc
    finally:
        await svc.stop()


@pytest.mark.asyncio
async def test_first_sighting_emits_added_immediately(service: DiscoveryService) -> None:
    desc = DeviceDescriptor(mac="aabbccddeeff", name="Workshop", transports={"tcp"})
    service._on_partial(desc)
    event = await asyncio.wait_for(service.__anext__(), timeout=1.0)
    assert isinstance(event, Added)
    assert event.device.mac == "aabbccddeeff"


@pytest.mark.asyncio
async def test_second_sighting_with_new_data_emits_updated(service: DiscoveryService) -> None:
    initial = DeviceDescriptor(mac="aabbccddeeff", transports={"tcp"})
    service._on_partial(initial)
    await asyncio.wait_for(service.__anext__(), timeout=1.0)  # consume Added

    augmented = DeviceDescriptor(
        mac="aabbccddeeff", transports={"ble"}, ble_address="AA:BB:CC:DD:EE:FF"
    )
    service._on_partial(augmented)
    event = await asyncio.wait_for(service.__anext__(), timeout=1.0)
    assert isinstance(event, Updated)
    assert event.device.transports == {"tcp", "ble"}


@pytest.mark.asyncio
async def test_repeated_identical_sightings_emit_no_updated(service: DiscoveryService) -> None:
    """Just refreshing last_seen / rssi shouldn't emit a stream of Updated events."""
    desc = DeviceDescriptor(mac="aabbccddeeff", transports={"tcp"}, name="Workshop")
    service._on_partial(desc)
    await asyncio.wait_for(service.__anext__(), timeout=1.0)  # consume Added

    # Same descriptor (identical fields) submitted again.
    service._on_partial(
        DeviceDescriptor(mac="aabbccddeeff", transports={"tcp"}, name="Workshop")
    )
    # Wait past the debounce window — no event should arrive.
    with pytest.raises(asyncio.TimeoutError):
        await asyncio.wait_for(service.__anext__(), timeout=0.5)


@pytest.mark.asyncio
async def test_burst_updates_are_debounced_to_one(service: DiscoveryService) -> None:
    """Multiple Updated triggers within 250ms collapse into one event with the latest data."""
    service._on_partial(DeviceDescriptor(mac="aabbccddeeff", transports={"tcp"}))
    await asyncio.wait_for(service.__anext__(), timeout=1.0)  # Added

    # Fire 5 changes in rapid succession.
    for i in range(5):
        service._on_partial(
            DeviceDescriptor(mac="aabbccddeeff", transports={"tcp"}, name=f"name-{i}")
        )

    event = await asyncio.wait_for(service.__anext__(), timeout=1.0)
    assert isinstance(event, Updated)
    assert event.device.name == "name-4"  # latest wins

    # No further Updated event should arrive.
    with pytest.raises(asyncio.TimeoutError):
        await asyncio.wait_for(service.__anext__(), timeout=0.4)


@pytest.mark.asyncio
async def test_devices_property_returns_snapshot(service: DiscoveryService) -> None:
    service._on_partial(DeviceDescriptor(mac="aabbccddeeff", name="Alpha"))
    service._on_partial(DeviceDescriptor(mac="112233445566", name="Beta"))

    snapshot = service.devices
    assert set(snapshot.keys()) == {"aabbccddeeff", "112233445566"}
    # Mutating the returned dict must not affect the service's internal state.
    snapshot.clear()
    assert len(service.devices) == 2


@pytest.mark.asyncio
async def test_two_separate_devices_each_get_added(service: DiscoveryService) -> None:
    service._on_partial(DeviceDescriptor(mac="aaaaaaaaaaaa", name="Alpha"))
    service._on_partial(DeviceDescriptor(mac="bbbbbbbbbbbb", name="Beta"))

    e1 = await asyncio.wait_for(service.__anext__(), timeout=1.0)
    e2 = await asyncio.wait_for(service.__anext__(), timeout=1.0)

    assert isinstance(e1, Added) and isinstance(e2, Added)
    macs = {e1.device.mac, e2.device.mac}
    assert macs == {"aaaaaaaaaaaa", "bbbbbbbbbbbb"}


@pytest.mark.asyncio
async def test_stale_cleanup_emits_removed() -> None:
    """Force-age a device past STALE_AFTER_S; cleanup loop should emit Removed."""
    from qnob_companion.discovery import service as svc_module

    # Tighten thresholds for the test.
    original_stale = svc_module.STALE_AFTER_S
    svc_module.STALE_AFTER_S = 0.1
    try:
        svc = DiscoveryService(enable_mdns=False, enable_ble=False)
        await svc.start()
        try:
            old = DeviceDescriptor(
                mac="aabbccddeeff",
                last_seen=datetime.now(tz=timezone.utc) - timedelta(seconds=10),
            )
            svc._on_partial(old)
            added = await asyncio.wait_for(svc.__anext__(), timeout=1.0)
            assert isinstance(added, Added)

            # Cleanup runs every STALE_AFTER_S/4 = 0.025s — wait long enough.
            event = await asyncio.wait_for(svc.__anext__(), timeout=2.0)
            assert isinstance(event, Removed)
            assert event.mac == "aabbccddeeff"
        finally:
            await svc.stop()
    finally:
        svc_module.STALE_AFTER_S = original_stale
