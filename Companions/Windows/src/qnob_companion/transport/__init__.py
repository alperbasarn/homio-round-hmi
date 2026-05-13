"""Transport layer — TCP + BLE + DeviceClient (#62 B3).

TCP transport is fully implemented; BLE is a stub pending the BLE half of #62.
"""

from qnob_companion.transport.base import (
    AsyncSignal,
    IdGenerator,
    Transport,
    TransportError,
    TransportState,
)
from qnob_companion.transport.ble import BLE_AVAILABLE, BleTransport
from qnob_companion.transport.client import DeviceClient
from qnob_companion.transport.tcp import TcpTransport

__all__ = [
    "BLE_AVAILABLE",
    "AsyncSignal",
    "BleTransport",
    "DeviceClient",
    "IdGenerator",
    "TcpTransport",
    "Transport",
    "TransportError",
    "TransportState",
]
