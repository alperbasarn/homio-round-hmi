"""Device detail dialog — tabbed config forms for a single paired device."""

from __future__ import annotations

import asyncio

from PySide6.QtCore import Qt, Slot
from PySide6.QtWidgets import (
    QDialog,
    QDialogButtonBox,
    QHBoxLayout,
    QLabel,
    QTabWidget,
    QVBoxLayout,
)

from qnob_companion.discovery.descriptor import DeviceDescriptor
from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.bluetooth_tab import BluetoothTab
from qnob_companion.ui.tabs.device_tab import DeviceTab
from qnob_companion.ui.tabs.display_tab import DisplayTab
from qnob_companion.ui.tabs.mqtt_tab import MqttTab
from qnob_companion.ui.tabs.wifi_tab import WiFiTab


class DeviceDetailDialog(QDialog):
    """Modal dialog showing config tabs for one connected device.

    Tabs are lazy-loaded: the first time a tab is selected its
    ``_load()`` coroutine runs to fetch current device state.
    """

    def __init__(
        self,
        device: DeviceDescriptor,
        client: DeviceClient,
        parent=None,
    ) -> None:
        super().__init__(parent)
        self._client = client
        self.setWindowTitle(device.name or device.mac)
        self.resize(640, 480)
        self.setMinimumSize(540, 400)

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)

        # ── Device header ────────────────────────────────────────────────
        header = self._build_header(device)
        root.addWidget(header)

        # ── Tab widget ───────────────────────────────────────────────────
        self._tabs = QTabWidget()
        self._tab_widgets: list[WiFiTab | MqttTab | BluetoothTab | DisplayTab | DeviceTab] = []

        for label, tab_cls in [
            ("Wi-Fi",     WiFiTab),
            ("MQTT",      MqttTab),
            ("Bluetooth", BluetoothTab),
            ("Display",   DisplayTab),
            ("Device",    DeviceTab),
        ]:
            tab = tab_cls(client)
            self._tab_widgets.append(tab)
            self._tabs.addTab(tab, label)

        self._tabs.currentChanged.connect(self._on_tab_changed)
        root.addWidget(self._tabs, 1)

        # ── Close button ─────────────────────────────────────────────────
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Close)
        buttons.rejected.connect(self.reject)
        root.addWidget(buttons)

        # Activate the first tab immediately.
        self._tab_widgets[0].activate()

    # ----- header builder -----

    @staticmethod
    def _build_header(device: DeviceDescriptor):
        from PySide6.QtWidgets import QFrame

        frame = QFrame()
        frame.setFrameShape(QFrame.Shape.StyledPanel)
        frame.setStyleSheet("QFrame { background: #f0f4f8; padding: 0; }")

        layout = QHBoxLayout(frame)
        layout.setContentsMargins(16, 10, 16, 10)

        name = QLabel(f"<b>{device.name or device.mac}</b>")
        layout.addWidget(name)

        details: list[str] = []
        if device.ip:
            details.append(str(device.ip))
        if device.firmware_version:
            details.append(f"fw {device.firmware_version}")
        details.append(device.mac)

        meta = QLabel("  ·  ".join(details))
        meta.setStyleSheet("color: #666; font-size: 11px;")
        layout.addWidget(meta, 1, Qt.AlignmentFlag.AlignRight)

        return frame

    # ----- tab lifecycle -----

    @Slot(int)
    def _on_tab_changed(self, index: int) -> None:
        if 0 <= index < len(self._tab_widgets):
            self._tab_widgets[index].activate()
