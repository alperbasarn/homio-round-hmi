"""BLE advertisement scanner for the Qnob service UUID."""

from __future__ import annotations

import logging
from collections.abc import Callable

from bleak import BleakScanner  # type: ignore[import-not-found]
from bleak.backends.device import BLEDevice  # type: ignore[import-not-found]
from bleak.backends.scanner import AdvertisementData  # type: ignore[import-not-found]

from qnob_companion.discovery.descriptor import DeviceDescriptor, canonical_mac

log = logging.getLogger(__name__)

# Custom 16-bit UUID in 128-bit form (per docs/protocol.md §2.2).
QNOB_SERVICE_UUID = "0000abf0-0000-1000-8000-00805f9b34fb"


DescriptorSink = Callable[[DeviceDescriptor], None]


def descriptor_from_advertisement(
    device: BLEDevice, ad_data: AdvertisementData
) -> DeviceDescriptor | None:
    """Build a partial DeviceDescriptor from a BLE advertisement.

    ``device.address`` is the BLE peripheral address (treated as the device's
    canonical MAC for now — see docstring on :class:`DeviceDescriptor`).
    Returns None if the address can't be canonicalised.
    """
    try:
        mac = canonical_mac(device.address)
    except ValueError:
        log.debug("Skipping BLE device with non-MAC address: %s", device.address)
        return None

    # Paired flag — manufacturer data byte 0 (per #59 A7).
    is_paired = False
    for _company_id, payload in (ad_data.manufacturer_data or {}).items():
        if payload and payload[0] == 0x01:
            is_paired = True
            break

    name = ad_data.local_name or device.name

    return DeviceDescriptor(
        mac=mac,
        name=name,
        ble_address=device.address,
        transports={"ble"},
        is_paired=is_paired,
        rssi_dbm=ad_data.rssi,
    )


class BleScanner:
    """Continuously scans for advertisements with the Qnob service UUID.

    Calls ``sink`` for every matching advertisement (which may be many per
    second per device — the DiscoveryService debounces).
    """

    def __init__(self, sink: DescriptorSink) -> None:
        self._sink = sink
        self._scanner: BleakScanner | None = None

    async def start(self) -> None:
        if self._scanner is not None:
            return
        self._scanner = BleakScanner(
            detection_callback=self._on_advertisement,
            service_uuids=[QNOB_SERVICE_UUID],
        )
        await self._scanner.start()
        log.info("BLE scanner started for service UUID %s", QNOB_SERVICE_UUID)

    async def stop(self) -> None:
        if self._scanner is None:
            return
        try:
            await self._scanner.stop()
        finally:
            self._scanner = None
        log.info("BLE scanner stopped")

    def _on_advertisement(self, device: BLEDevice, ad_data: AdvertisementData) -> None:
        descriptor = descriptor_from_advertisement(device, ad_data)
        if descriptor is not None:
            try:
                self._sink(descriptor)
            except Exception:  # noqa: BLE001 — sink errors must not break scanner
                log.exception("Discovery sink raised on BLE advertisement")
