"""Transport layer — TCP + BLE + Serial + DeviceClient (#62 B3).

TCP and BLE transports are fully implemented; Serial provides wired USB-CDC
pairing bootstrap and an interactive terminal.
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
from qnob_companion.transport.serial import SERIAL_AVAILABLE, SerialTransport
from qnob_companion.transport.tcp import TcpTransport

__all__ = [
    "BLE_AVAILABLE",
    "AsyncSignal",
    "BleTransport",
    "DeviceClient",
    "IdGenerator",
    "SERIAL_AVAILABLE",
    "SerialTransport",
    "TcpTransport",
    "Transport",
    "TransportError",
    "TransportState",
]
