"""Status tab — polls device status every 5 s while visible."""

from __future__ import annotations

import asyncio
import logging
from typing import Any

from PySide6.QtCore import Qt
from PySide6.QtGui import QHideEvent, QShowEvent
from PySide6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QWidget

from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.base_tab import BaseTab

log = logging.getLogger(__name__)

_POLL_INTERVAL = 5.0


class StatusTab(BaseTab):
    """Live device status view; auto-refreshes every 5 s while the tab is open."""

    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        self._uptime_lbl = QLabel("—")
        self._free_heap_lbl = QLabel("—")
        self._wifi_lbl = QLabel("—")
        self._ip_lbl = QLabel("—")
        self._mqtt_lbl = QLabel("—")
        self._ble_lbl = QLabel("—")
        self._refresh_btn = QPushButton("Refresh")
        self._poll_task: asyncio.Task[None] | None = None
        self._polling = False

        super().__init__(client, parent)
        self._refresh_btn.clicked.connect(
            lambda: asyncio.ensure_future(self._poll_once())
        )

    # ----- form builder -----

    def _build_form(self) -> None:
        f = self._form
        f.addRow("Uptime", self._uptime_lbl)
        f.addRow("Free heap", self._free_heap_lbl)
        f.addRow("", QLabel(""))
        f.addRow("Wi-Fi", self._wifi_lbl)
        f.addRow("IP address", self._ip_lbl)
        f.addRow("", QLabel(""))
        f.addRow("MQTT", self._mqtt_lbl)
        f.addRow("BLE centrals", self._ble_lbl)
        f.addRow("", QLabel(""))

        row = QHBoxLayout()
        row.addWidget(self._refresh_btn)
        row.addStretch()
        f.addRow("", row)

    # ----- lifecycle -----

    def activate(self) -> None:
        """Called by DeviceDetailDialog each time this tab is selected."""
        self._ensure_poll_running()

    def showEvent(self, event: QShowEvent) -> None:
        super().showEvent(event)
        self._ensure_poll_running()

    def hideEvent(self, event: QHideEvent) -> None:
        super().hideEvent(event)
        self._stop_poll()

    # ----- poll management -----

    def _ensure_poll_running(self) -> None:
        if self._poll_task is None or self._poll_task.done():
            self._poll_task = asyncio.ensure_future(self._poll_loop())

    def _stop_poll(self) -> None:
        if self._poll_task is not None and not self._poll_task.done():
            self._poll_task.cancel()
        self._poll_task = None

    async def _poll_loop(self) -> None:
        try:
            while True:
                await self._poll_once()
                await asyncio.sleep(_POLL_INTERVAL)
        except asyncio.CancelledError:
            pass

    async def _poll_once(self) -> None:
        if self._polling:
            return
        self._polling = True
        try:
            resp = await self._client.send_command("status")
            if resp.is_ok and resp.data:
                self._update_display(resp.data)
            else:
                self._toast("Could not read device status", error=True)
        except Exception as exc:
            log.debug("Status poll failed: %s", exc)
        finally:
            self._polling = False

    # ----- display -----

    def _update_display(self, data: dict[str, Any]) -> None:
        uptime_ms = data.get("uptime_ms", 0)
        self._uptime_lbl.setText(_fmt_uptime(uptime_ms))

        heap = data.get("free_heap", 0)
        self._free_heap_lbl.setText(f"{heap // 1024} KB  ({heap:,} bytes)")

        ssid = data.get("wifi_ssid", "")
        rssi = data.get("wifi_rssi")
        if ssid:
            rssi_str = f"  ({rssi} dBm)" if rssi is not None else ""
            self._wifi_lbl.setText(f"{ssid}{rssi_str}")
        else:
            self._wifi_lbl.setText("Not connected")

        ip = data.get("ip") or "—"
        self._ip_lbl.setText(str(ip))

        mqtt = data.get("mqtt_connected", False)
        self._mqtt_lbl.setText("Connected" if mqtt else "Disconnected")
        self._mqtt_lbl.setStyleSheet(
            "color: #27ae60;" if mqtt else "color: #c0392b;"
        )

        ble_centrals = data.get("ble_centrals") or []
        if ble_centrals:
            self._ble_lbl.setText(
                f"{len(ble_centrals)} — {', '.join(str(x) for x in ble_centrals)}"
            )
        else:
            self._ble_lbl.setText("None")


def _fmt_uptime(ms: int) -> str:
    s = ms // 1000
    h, rem = divmod(s, 3600)
    m, s = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{s:02d}"
