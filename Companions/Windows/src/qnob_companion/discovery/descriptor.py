"""DeviceDescriptor and DiscoveryEvent types."""

from __future__ import annotations

from dataclasses import dataclass, field, replace
from datetime import datetime, timezone
from ipaddress import IPv4Address
from typing import Literal

TransportType = Literal["tcp", "ble"]


def _utcnow() -> datetime:
    return datetime.now(tz=timezone.utc)


def canonical_mac(value: str) -> str:
    """Normalise a MAC string to 12 lowercase hex chars with no separators.

    Accepts: ``AA:BB:CC:DD:EE:FF``, ``AA-BB-CC-DD-EE-FF``, ``aabbccddeeff``.
    Returns: ``aabbccddeeff``. Raises ``ValueError`` if length is wrong after stripping.
    """
    cleaned = value.replace(":", "").replace("-", "").lower()
    if len(cleaned) != 12 or any(c not in "0123456789abcdef" for c in cleaned):
        raise ValueError(f"not a MAC: {value!r}")
    return cleaned


@dataclass(slots=True)
class DeviceDescriptor:
    """One discoverable Qnob device.

    The ``mac`` field is the canonical merge key. mDNS provides it via the TXT
    record ``mac=...``; BLE provides the peripheral address. On firmware where
    those differ (ESP32 BT_MAC = WIFI_MAC + 2 by default), the same physical
    device may appear as two descriptors — the firmware will need to either
    advertise STA-MAC in BLE manufacturer data or use a unified address.
    """

    mac: str
    name: str | None = None
    firmware_version: str | None = None
    ip: IPv4Address | None = None
    tcp_port: int | None = None
    ble_address: str | None = None
    transports: set[TransportType] = field(default_factory=set)
    is_paired: bool = False
    rssi_dbm: int | None = None
    last_seen: datetime = field(default_factory=_utcnow)

    def equivalent_to(self, other: DeviceDescriptor) -> bool:
        """True when all user-visible fields match (ignores ``last_seen`` and ``rssi_dbm``).

        Used by the discovery service to decide whether a fresh sighting is a
        no-op refresh (don't emit Updated) versus a real change.
        """
        return (
            self.mac == other.mac
            and self.name == other.name
            and self.firmware_version == other.firmware_version
            and self.ip == other.ip
            and self.tcp_port == other.tcp_port
            and self.ble_address == other.ble_address
            and self.transports == other.transports
            and self.is_paired == other.is_paired
        )

    def merged_with(self, other: DeviceDescriptor) -> DeviceDescriptor:
        """Return a copy of self updated with non-None fields from ``other``.

        Transports are unioned. ``last_seen`` and ``rssi_dbm`` always take the
        newer value.
        """
        if self.mac != other.mac:
            raise ValueError("cannot merge descriptors with different macs")
        return replace(
            self,
            name=other.name or self.name,
            firmware_version=other.firmware_version or self.firmware_version,
            ip=other.ip if other.ip is not None else self.ip,
            tcp_port=other.tcp_port if other.tcp_port is not None else self.tcp_port,
            ble_address=other.ble_address or self.ble_address,
            transports=self.transports | other.transports,
            is_paired=other.is_paired,
            rssi_dbm=other.rssi_dbm if other.rssi_dbm is not None else self.rssi_dbm,
            last_seen=other.last_seen,
        )


# ─── Discovery events emitted by DiscoveryService ───────────────────────────


@dataclass(slots=True, frozen=True)
class Added:
    device: DeviceDescriptor


@dataclass(slots=True, frozen=True)
class Updated:
    device: DeviceDescriptor


@dataclass(slots=True, frozen=True)
class Removed:
    mac: str


DiscoveryEvent = Added | Updated | Removed
