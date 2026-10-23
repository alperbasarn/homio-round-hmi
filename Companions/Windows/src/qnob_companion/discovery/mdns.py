"""mDNS browser for ``_qnob._tcp`` services."""

from __future__ import annotations

import asyncio
import logging
from collections.abc import Callable
from ipaddress import IPv4Address, ip_address
from typing import Any

from zeroconf import ServiceStateChange  # type: ignore[import-not-found]
from zeroconf.asyncio import AsyncServiceBrowser, AsyncServiceInfo, AsyncZeroconf  # type: ignore[import-not-found]

from qnob_companion.discovery.descriptor import DeviceDescriptor, canonical_mac

log = logging.getLogger(__name__)

SERVICE_TYPE = "_qnob._tcp.local."
SUPPORTED_PROTO_VERSION = 1


# Sink type: called with a fresh DeviceDescriptor on each TXT-record refresh.
DescriptorSink = Callable[[DeviceDescriptor], None]


def parse_txt_records(txt: dict[bytes, bytes | None]) -> dict[str, str]:
    """Convert zeroconf's bytes-keyed TXT dict to a clean str→str dict."""
    out: dict[str, str] = {}
    for k, v in txt.items():
        if v is None:
            continue
        try:
            out[k.decode("utf-8", errors="strict")] = v.decode("utf-8", errors="replace")
        except UnicodeDecodeError:
            continue
    return out


def descriptor_from_service_info(info: AsyncServiceInfo) -> DeviceDescriptor | None:
    """Build a DeviceDescriptor from a resolved zeroconf ServiceInfo.

    Returns None if the service announces a protocol version we don't speak,
    or if the required ``mac`` TXT record is missing/malformed.
    """
    txt = parse_txt_records(info.properties or {})

    proto_str = txt.get("proto", "0")
    try:
        proto = int(proto_str)
    except ValueError:
        log.warning("Ignoring %s: malformed proto=%r", info.name, proto_str)
        return None
    if proto > SUPPORTED_PROTO_VERSION:
        log.warning(
            "Ignoring %s: proto=%d > supported %d", info.name, proto, SUPPORTED_PROTO_VERSION
        )
        return None

    mac_raw = txt.get("mac")
    if not mac_raw:
        log.warning("Ignoring %s: missing mac TXT record", info.name)
        return None
    try:
        mac = canonical_mac(mac_raw)
    except ValueError as exc:
        log.warning("Ignoring %s: %s", info.name, exc)
        return None

    # Prefer IPv4 if the device announced one.
    ip: IPv4Address | None = None
    for addr_bytes in info.addresses or ():
        try:
            parsed = ip_address(addr_bytes)
        except ValueError:
            continue
        if isinstance(parsed, IPv4Address):
            ip = parsed
            break

    caps = {c.strip() for c in txt.get("caps", "").split(",") if c.strip()}
    transports: set[Any] = set()
    if "tcp" in caps:
        transports.add("tcp")
    if "ble" in caps:
        transports.add("ble")
    transports.add("tcp")  # implied by being on _qnob._tcp

    return DeviceDescriptor(
        mac=mac,
        name=txt.get("name"),
        firmware_version=txt.get("fw"),
        ip=ip,
        tcp_port=info.port,
        transports=transports,
        is_paired=txt.get("paired") == "1",
    )


class MdnsBrowser:
    """Subscribes to ``_qnob._tcp`` and pushes DeviceDescriptors to a sink."""

    RESOLVE_TIMEOUT_MS = 3000

    def __init__(self, sink: DescriptorSink) -> None:
        self._sink = sink
        self._zc: AsyncZeroconf | None = None
        self._browser: AsyncServiceBrowser | None = None
        self._resolve_tasks: set[asyncio.Task[None]] = set()

    async def start(self) -> None:
        if self._zc is not None:
            return
        self._zc = AsyncZeroconf()
        self._browser = AsyncServiceBrowser(
            self._zc.zeroconf,
            [SERVICE_TYPE],
            handlers=[self._on_state_change],
        )
        log.info("mDNS browser started for %s", SERVICE_TYPE)

    async def stop(self) -> None:
        for task in list(self._resolve_tasks):
            task.cancel()
        for task in list(self._resolve_tasks):
            try:
                await task
            except (asyncio.CancelledError, Exception):
                pass
        self._resolve_tasks.clear()

        if self._browser is not None:
            await self._browser.async_cancel()
            self._browser = None
        if self._zc is not None:
            await self._zc.async_close()
            self._zc = None
        log.info("mDNS browser stopped")

    def _on_state_change(
        self,
        zeroconf: Any,
        service_type: str,
        name: str,
        state_change: ServiceStateChange,
    ) -> None:
        # Adds and updates both warrant a fresh resolve. Removed names just log.
        if state_change == ServiceStateChange.Removed:
            log.debug("Service removed: %s", name)
            return
        task = asyncio.create_task(self._resolve(service_type, name), name=f"mdns-resolve-{name}")
        self._resolve_tasks.add(task)
        task.add_done_callback(self._resolve_tasks.discard)

    async def _resolve(self, service_type: str, name: str) -> None:
        if self._zc is None:
            return
        info = AsyncServiceInfo(service_type, name)
        ok = await info.async_request(self._zc.zeroconf, self.RESOLVE_TIMEOUT_MS)
        if not ok:
            log.debug("mDNS resolve failed for %s", name)
            return
        descriptor = descriptor_from_service_info(info)
        if descriptor is not None:
            self._sink(descriptor)
