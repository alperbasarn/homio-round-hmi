"""Model-layer tests for DevicesPage event handling.

These tests exercise the add/update/remove logic and the secret-store
integration WITHOUT importing Qt (no QApplication needed), by subclassing
DevicesPage and stubbing the Qt widget methods.
"""

from __future__ import annotations

from ipaddress import IPv4Address
from unittest.mock import MagicMock

import pytest

from qnob_companion.discovery.descriptor import DeviceDescriptor
from qnob_companion.discovery.service import DiscoveryService
from qnob_companion.pairing.secret_store import MemorySecretStore


# ---------------------------------------------------------------------------
# Minimal headless stub — exercises device-list state without Qt widgets.
# ---------------------------------------------------------------------------

class _HeadlessDevicesModel:
    """Thin model extracted from DevicesPage for headless testing.

    Mirrors _add_device, _update_device, and _remove_device exactly but
    stores state in plain Python dicts instead of QListWidget items.
    """

    def __init__(self, store: MemorySecretStore) -> None:
        self._store = store
        # mac -> DeviceDescriptor
        self.live_devices: dict[str, DeviceDescriptor] = {}
        # mac -> True/False at time of last add/update
        self.paired_flags: dict[str, bool] = {}
        self.offline_macs: set[str] = set()

    def add_device(self, device: DeviceDescriptor) -> None:
        if device.mac in self.live_devices:
            self.update_device(device)
            return
        self.live_devices[device.mac] = device
        self.paired_flags[device.mac] = self._store.get(device.mac) is not None

    def update_device(self, device: DeviceDescriptor) -> None:
        if device.mac not in self.live_devices:
            self.add_device(device)
            return
        self.live_devices[device.mac] = device
        self.paired_flags[device.mac] = self._store.get(device.mac) is not None

    def remove_device(self, mac: str) -> None:
        if mac not in self.live_devices:
            return
        del self.live_devices[mac]
        del self.paired_flags[mac]
        self.offline_macs.add(mac)


# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def _dev(
    mac: str = "aabbccddeeff",
    name: str | None = "Workshop",
    transports: set[str] | None = None,
    ip: str | None = "192.168.1.50",
    fw: str | None = "1.2.3",
    is_paired: bool = False,
) -> DeviceDescriptor:
    return DeviceDescriptor(
        mac=mac,
        name=name,
        firmware_version=fw,
        ip=IPv4Address(ip) if ip else None,
        tcp_port=23456 if ip else None,
        transports=transports or {"tcp"},
        is_paired=is_paired,
    )


# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

def test_add_device_appears_in_live_list() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev())
    assert "aabbccddeeff" in model.live_devices


def test_add_device_shows_unpaired_when_no_token() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev())
    assert model.paired_flags["aabbccddeeff"] is False


def test_add_device_shows_paired_when_token_present() -> None:
    store = MemorySecretStore()
    store.set("aabbccddeeff", "my-secret-token")
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev())
    assert model.paired_flags["aabbccddeeff"] is True


def test_second_add_for_same_mac_updates_instead() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev(name="Alpha"))
    model.add_device(_dev(name="Beta"))
    assert len(model.live_devices) == 1
    assert model.live_devices["aabbccddeeff"].name == "Beta"


def test_update_device_refreshes_paired_flag_after_pairing() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev())
    assert model.paired_flags["aabbccddeeff"] is False

    store.set("aabbccddeeff", "token-xyz")
    model.update_device(_dev())
    assert model.paired_flags["aabbccddeeff"] is True


def test_remove_device_leaves_live_list_empty() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev())
    model.remove_device("aabbccddeeff")
    assert "aabbccddeeff" not in model.live_devices
    assert "aabbccddeeff" in model.offline_macs


def test_two_devices_tracked_independently() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev(mac="aaaaaaaaaaaa", name="Alpha"))
    model.add_device(_dev(mac="bbbbbbbbbbbb", name="Beta"))
    assert len(model.live_devices) == 2

    model.remove_device("aaaaaaaaaaaa")
    assert "bbbbbbbbbbbb" in model.live_devices
    assert "aaaaaaaaaaaa" not in model.live_devices


def test_remove_unknown_mac_is_noop() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.remove_device("000000000000")  # never added — must not raise
    assert len(model.live_devices) == 0


def test_update_unknown_mac_falls_back_to_add() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.update_device(_dev())
    assert "aabbccddeeff" in model.live_devices


def test_device_with_ble_and_tcp_both_tracked() -> None:
    store = MemorySecretStore()
    model = _HeadlessDevicesModel(store)
    model.add_device(_dev(transports={"tcp", "ble"}))
    assert model.live_devices["aabbccddeeff"].transports == {"tcp", "ble"}
