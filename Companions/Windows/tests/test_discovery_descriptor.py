"""Tests for DeviceDescriptor merging and canonical_mac normalisation."""

from __future__ import annotations

from datetime import datetime, timedelta, timezone
from ipaddress import IPv4Address

import pytest

from qnob_companion.discovery import DeviceDescriptor, canonical_mac


def test_canonical_mac_strips_separators_and_lowercases() -> None:
    assert canonical_mac("AA:BB:CC:DD:EE:FF") == "aabbccddeeff"
    assert canonical_mac("aa-bb-cc-dd-ee-ff") == "aabbccddeeff"
    assert canonical_mac("AABBCCDDEEFF") == "aabbccddeeff"


def test_canonical_mac_rejects_bad_input() -> None:
    with pytest.raises(ValueError):
        canonical_mac("not-a-mac")
    with pytest.raises(ValueError):
        canonical_mac("aabbcc")  # too short
    with pytest.raises(ValueError):
        canonical_mac("zzzzzzzzzzzz")  # non-hex


def test_merged_with_unions_transports() -> None:
    a = DeviceDescriptor(mac="aabbccddeeff", transports={"tcp"}, ip=IPv4Address("192.168.1.5"))
    b = DeviceDescriptor(mac="aabbccddeeff", transports={"ble"}, ble_address="AA:BB:CC:DD:EE:FF")
    merged = a.merged_with(b)
    assert merged.transports == {"tcp", "ble"}
    assert merged.ip == IPv4Address("192.168.1.5")
    assert merged.ble_address == "AA:BB:CC:DD:EE:FF"


def test_merged_with_takes_newer_paired_state() -> None:
    a = DeviceDescriptor(mac="aabbccddeeff", is_paired=False)
    b = DeviceDescriptor(mac="aabbccddeeff", is_paired=True)
    assert a.merged_with(b).is_paired is True


def test_merged_with_keeps_existing_when_other_has_none() -> None:
    a = DeviceDescriptor(mac="aabbccddeeff", name="Workshop", firmware_version="1.2.3")
    b = DeviceDescriptor(mac="aabbccddeeff")  # no name or fw
    merged = a.merged_with(b)
    assert merged.name == "Workshop"
    assert merged.firmware_version == "1.2.3"


def test_merged_with_uses_other_last_seen() -> None:
    earlier = datetime.now(tz=timezone.utc) - timedelta(seconds=30)
    later = datetime.now(tz=timezone.utc)
    a = DeviceDescriptor(mac="aabbccddeeff", last_seen=earlier)
    b = DeviceDescriptor(mac="aabbccddeeff", last_seen=later)
    assert a.merged_with(b).last_seen == later


def test_merged_with_rejects_different_macs() -> None:
    a = DeviceDescriptor(mac="aaaaaaaaaaaa")
    b = DeviceDescriptor(mac="bbbbbbbbbbbb")
    with pytest.raises(ValueError):
        a.merged_with(b)
