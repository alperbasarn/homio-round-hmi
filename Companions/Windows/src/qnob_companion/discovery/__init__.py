"""Device discovery — mDNS browser + BLE scanner unified into one event stream."""

from qnob_companion.discovery.descriptor import (
    Added,
    DeviceDescriptor,
    DiscoveryEvent,
    Removed,
    TransportType,
    Updated,
    canonical_mac,
)
from qnob_companion.discovery.service import DiscoveryService

__all__ = [
    "Added",
    "DeviceDescriptor",
    "DiscoveryEvent",
    "DiscoveryService",
    "Removed",
    "TransportType",
    "Updated",
    "canonical_mac",
]
