"""Tests for the mDNS TXT-record parser."""

from __future__ import annotations

from ipaddress import IPv4Address
from typing import Any

import pytest

from qnob_companion.discovery.mdns import (
    descriptor_from_service_info,
    parse_txt_records,
)


def test_parse_txt_records_decodes_bytes_to_strings() -> None:
    raw = {b"name": b"Workshop", b"fw": b"1.2.3-abcd", b"proto": b"1"}
    parsed = parse_txt_records(raw)
    assert parsed == {"name": "Workshop", "fw": "1.2.3-abcd", "proto": "1"}


def test_parse_txt_records_skips_none_values() -> None:
    parsed = parse_txt_records({b"name": b"x", b"empty": None})
    assert "empty" not in parsed
    assert parsed["name"] == "x"


class _FakeServiceInfo:
    """Minimal stand-in for zeroconf AsyncServiceInfo for descriptor parsing."""

    def __init__(
        self,
        properties: dict[bytes, bytes | None],
        addresses: list[bytes] | None = None,
        port: int | None = 23456,
        name: str = "qnob-aabb._qnob._tcp.local.",
    ) -> None:
        self.properties = properties
        self.addresses = addresses or [IPv4Address("192.168.1.50").packed]
        self.port = port
        self.name = name


def test_descriptor_from_service_info_full_txt() -> None:
    info: Any = _FakeServiceInfo(
        properties={
            b"name": b"Workshop",
            b"fw": b"1.2.3",
            b"mac": b"AA:BB:CC:DD:EE:FF",
            b"caps": b"tcp,ble",
            b"proto": b"1",
            b"paired": b"1",
        }
    )
    desc = descriptor_from_service_info(info)
    assert desc is not None
    assert desc.mac == "aabbccddeeff"
    assert desc.name == "Workshop"
    assert desc.firmware_version == "1.2.3"
    assert desc.ip == IPv4Address("192.168.1.50")
    assert desc.tcp_port == 23456
    assert desc.transports == {"tcp", "ble"}
    assert desc.is_paired is True


def test_descriptor_from_service_info_missing_mac_returns_none() -> None:
    info: Any = _FakeServiceInfo(properties={b"name": b"x", b"proto": b"1"})
    assert descriptor_from_service_info(info) is None


def test_descriptor_from_service_info_unsupported_proto_returns_none() -> None:
    info: Any = _FakeServiceInfo(
        properties={b"mac": b"aabbccddeeff", b"proto": b"99"},
    )
    assert descriptor_from_service_info(info) is None


def test_descriptor_from_service_info_malformed_proto_returns_none() -> None:
    info: Any = _FakeServiceInfo(
        properties={b"mac": b"aabbccddeeff", b"proto": b"not-a-number"},
    )
    assert descriptor_from_service_info(info) is None


def test_descriptor_from_service_info_paired_false_when_zero() -> None:
    info: Any = _FakeServiceInfo(
        properties={b"mac": b"aabbccddeeff", b"proto": b"1", b"paired": b"0"},
    )
    desc = descriptor_from_service_info(info)
    assert desc is not None
    assert desc.is_paired is False


def test_descriptor_from_service_info_implies_tcp_when_caps_missing() -> None:
    """Even without an explicit caps TXT, presence on _qnob._tcp implies TCP."""
    info: Any = _FakeServiceInfo(
        properties={b"mac": b"aabbccddeeff", b"proto": b"1"},
    )
    desc = descriptor_from_service_info(info)
    assert desc is not None
    assert "tcp" in desc.transports
