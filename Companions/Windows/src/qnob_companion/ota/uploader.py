"""OtaUploader — chunked binary firmware push over TCP.

Protocol (docs/protocol.md §4.10):
  1. otaBegin {size, sha256} → {upload_id}
  2. otaChunk {upload_id, offset, data:"base64"} → {acked_offset}  (loop)
  3. Device verifies hash after final acked_offset == size, then reboots.
  4. otaAbort {upload_id} → cancels and rolls back.

OTA is TCP-only; BLE throughput is insufficient.
"""

from __future__ import annotations

import asyncio
import base64
import hashlib
import time
from collections.abc import Callable
from pathlib import Path

from qnob_companion.transport.client import DeviceClient
from qnob_companion.transport.base import TransportState

MAX_FIRMWARE_BYTES = 4 * 1024 * 1024   # 4 MiB
CHUNK_SIZE = 4096                        # raw bytes per chunk (per spec)


class OtaError(Exception):
    """Raised when the OTA upload fails or is rejected by the device."""


# Signature: (acked_bytes, total_bytes, bytes_per_sec, eta_seconds)
ProgressCallback = Callable[[int, int, float, float], None]


class OtaUploader:
    """Manages one OTA upload session.

    Usage::

        uploader = OtaUploader(client, Path("firmware.bin"))
        await uploader.start(on_progress=lambda a, t, r, e: ...)
        # uploader.cancel() can be called concurrently to abort.
    """

    def __init__(self, client: DeviceClient, firmware_path: Path) -> None:
        self._client = client
        self._path = firmware_path
        self._upload_id: str | None = None
        self._cancel_event = asyncio.Event()

    # ----- public API -----

    @property
    def is_tcp_available(self) -> bool:
        """False when no TCP transport is connected (OTA requires TCP)."""
        tcp = self._client._tcp  # type: ignore[attr-defined]
        return tcp is not None and tcp.state == TransportState.CONNECTED

    async def start(self, on_progress: ProgressCallback | None = None) -> None:
        """Run the full upload sequence.

        Raises :class:`OtaError` on device rejection or protocol error.
        Cancels cleanly (``otaAbort``) when ``cancel()`` is called.
        """
        if not self.is_tcp_available:
            raise OtaError("OTA requires an active TCP connection")

        data = self._path.read_bytes()
        if len(data) > MAX_FIRMWARE_BYTES:
            raise OtaError(
                f"Firmware file is {len(data) / 1024 / 1024:.1f} MiB — maximum is 4 MiB"
            )

        sha256 = hashlib.sha256(data).hexdigest()
        total = len(data)

        # ── 1. Begin ──────────────────────────────────────────────────────
        resp = await self._client.send_command(
            "otaBegin", {"size": total, "sha256": sha256}
        )
        if not resp.is_ok:
            raise OtaError(f"otaBegin rejected: {resp.error} — {resp.detail}")
        upload_id = (resp.data or {}).get("upload_id")
        if not upload_id:
            raise OtaError("otaBegin returned no upload_id")
        self._upload_id = upload_id

        # ── 2. Chunks ─────────────────────────────────────────────────────
        offset = 0
        t_start = time.monotonic()

        while offset < total:
            if self._cancel_event.is_set():
                await self._abort()
                raise OtaError("Upload cancelled")

            chunk_raw = data[offset : offset + CHUNK_SIZE]
            chunk_b64 = base64.b64encode(chunk_raw).decode("ascii")

            resp = await self._client.send_command(
                "otaChunk",
                {"upload_id": upload_id, "offset": offset, "data": chunk_b64},
                timeout=30.0,   # device flash write can be slow
            )
            if not resp.is_ok:
                raise OtaError(f"otaChunk failed at offset {offset}: {resp.error}")

            acked = (resp.data or {}).get("acked_offset", offset + len(chunk_raw))
            offset = acked

            if on_progress is not None:
                elapsed = time.monotonic() - t_start
                rate = acked / elapsed if elapsed > 0 else 0.0
                remaining = total - acked
                eta = remaining / rate if rate > 0 else 0.0
                on_progress(acked, total, rate, eta)

        # ── 3. Done — device verifies hash and reboots ────────────────────
        self._upload_id = None

    async def cancel(self) -> None:
        """Signal cancellation; the running ``start()`` will abort cleanly."""
        self._cancel_event.set()

    # ----- internals -----

    async def _abort(self) -> None:
        if self._upload_id is None:
            return
        uid = self._upload_id
        self._upload_id = None
        try:
            await self._client.send_command("otaAbort", {"upload_id": uid}, timeout=5.0)
        except Exception:
            pass   # best-effort; device will time out the session itself
