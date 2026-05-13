"""DiscoveryService — merges mDNS + BLE streams into one debounced event flow."""

from __future__ import annotations

import asyncio
import contextlib
import logging
from collections.abc import AsyncIterator

from qnob_companion.discovery.ble import BleScanner
from qnob_companion.discovery.descriptor import (
    Added,
    DeviceDescriptor,
    DiscoveryEvent,
    Removed,
    Updated,
)
from qnob_companion.discovery.mdns import MdnsBrowser

log = logging.getLogger(__name__)

# Per-device debounce window for Updated events (seconds). Added events are
# emitted immediately. Removed events come from the stale-cleanup task.
UPDATE_DEBOUNCE_S = 0.25
STALE_AFTER_S = 60.0  # device unseen for this long → Removed


class DiscoveryService:
    """Asynchronous device discovery combining mDNS and BLE.

    Usage::

        svc = DiscoveryService()
        await svc.start()
        async for event in svc:
            ...

    Emits ``Added`` immediately for new MACs, debounced ``Updated`` events
    when descriptor fields change, and ``Removed`` when a device hasn't been
    re-seen within ``STALE_AFTER_S``.
    """

    def __init__(
        self,
        *,
        enable_mdns: bool = True,
        enable_ble: bool = True,
    ) -> None:
        self._enable_mdns = enable_mdns
        self._enable_ble = enable_ble
        self._mdns: MdnsBrowser | None = None
        self._ble: BleScanner | None = None
        self._devices: dict[str, DeviceDescriptor] = {}
        self._events: asyncio.Queue[DiscoveryEvent] = asyncio.Queue()
        self._pending_updates: dict[str, asyncio.Task[None]] = {}
        self._stop = asyncio.Event()
        self._cleanup_task: asyncio.Task[None] | None = None

    # ----- lifecycle -----

    async def start(self) -> None:
        if self._enable_mdns and self._mdns is None:
            self._mdns = MdnsBrowser(sink=self._on_partial)
            await self._mdns.start()
        if self._enable_ble and self._ble is None:
            self._ble = BleScanner(sink=self._on_partial)
            try:
                await self._ble.start()
            except Exception:  # noqa: BLE001 — BLE may be unavailable on the host
                log.warning("BLE scanner failed to start; continuing with mDNS only", exc_info=True)
                self._ble = None
        if self._cleanup_task is None or self._cleanup_task.done():
            self._cleanup_task = asyncio.create_task(self._stale_cleanup_loop(), name="disc-cleanup")

    async def stop(self) -> None:
        self._stop.set()

        for task in list(self._pending_updates.values()):
            if not task.done():
                task.cancel()
        for task in list(self._pending_updates.values()):
            with contextlib.suppress(asyncio.CancelledError, Exception):
                await task
        self._pending_updates.clear()

        if self._cleanup_task is not None:
            self._cleanup_task.cancel()
            with contextlib.suppress(asyncio.CancelledError):
                await self._cleanup_task
            self._cleanup_task = None

        if self._mdns is not None:
            await self._mdns.stop()
            self._mdns = None
        if self._ble is not None:
            await self._ble.stop()
            self._ble = None

    # ----- public read API -----

    @property
    def devices(self) -> dict[str, DeviceDescriptor]:
        """Snapshot of all currently-known devices, keyed by canonical MAC."""
        return dict(self._devices)

    def __aiter__(self) -> AsyncIterator[DiscoveryEvent]:
        return self

    async def __anext__(self) -> DiscoveryEvent:
        return await self._events.get()

    # ----- merge / debounce -----

    def _on_partial(self, partial: DeviceDescriptor) -> None:
        existing = self._devices.get(partial.mac)
        if existing is None:
            self._devices[partial.mac] = partial
            self._events.put_nowait(Added(partial))
            return

        merged = existing.merged_with(partial)
        # Refresh stored copy in either branch (so last_seen / rssi stay current).
        self._devices[partial.mac] = merged
        if merged.equivalent_to(existing):
            return  # only volatile fields changed — don't notify subscribers
        self._schedule_update(partial.mac)

    def _schedule_update(self, mac: str) -> None:
        existing = self._pending_updates.get(mac)
        if existing is not None and not existing.done():
            existing.cancel()
        self._pending_updates[mac] = asyncio.create_task(
            self._delayed_emit_update(mac), name=f"disc-update-{mac}"
        )

    async def _delayed_emit_update(self, mac: str) -> None:
        try:
            await asyncio.sleep(UPDATE_DEBOUNCE_S)
        except asyncio.CancelledError:
            return
        device = self._devices.get(mac)
        if device is not None:
            self._events.put_nowait(Updated(device))
        self._pending_updates.pop(mac, None)

    # ----- stale cleanup -----

    async def _stale_cleanup_loop(self) -> None:
        from datetime import datetime, timezone

        try:
            while not self._stop.is_set():
                try:
                    await asyncio.wait_for(self._stop.wait(), timeout=STALE_AFTER_S / 4)
                    return
                except asyncio.TimeoutError:
                    pass

                now = datetime.now(tz=timezone.utc)
                for mac, device in list(self._devices.items()):
                    age = (now - device.last_seen).total_seconds()
                    if age > STALE_AFTER_S:
                        self._devices.pop(mac, None)
                        self._events.put_nowait(Removed(mac))
                        log.debug("Removed stale device %s (last seen %.0fs ago)", mac, age)
        except asyncio.CancelledError:
            raise
