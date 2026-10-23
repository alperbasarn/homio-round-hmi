"""Device settings tab — name, calibration, restart, factory reset."""

from __future__ import annotations

import asyncio

from PySide6.QtWidgets import (
    QLabel,
    QLineEdit,
    QMessageBox,
    QPushButton,
    QWidget,
)

from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.base_tab import BaseTab


class DeviceTab(BaseTab):
    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        self._name_edit = QLineEdit()
        self._name_edit.setPlaceholderText("Homio-0000")
        self._name_btn = QPushButton("Set Device Name")

        self._calibrate_btn = QPushButton("Calibrate Orientation")
        self._restart_btn = QPushButton("Restart Device")
        self._factory_btn = QPushButton("Factory Reset (Wipe All)")
        self._factory_btn.setStyleSheet(
            "QPushButton { color: #c0392b; border: 1px solid #c0392b;"
            " border-radius: 4px; padding: 4px 8px; }"
        )
        super().__init__(client, parent)
        self._name_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_set_name()))
        self._calibrate_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_calibrate()))
        self._restart_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_restart()))
        self._factory_btn.clicked.connect(self._on_factory_reset_clicked)

    def _build_form(self) -> None:
        f = self._form
        f.addRow("Device name", self._name_edit)
        f.addRow("", QLabel("Name format: Homio-XXXX  (A–Z, 0–9)"))
        f.addRow("", self._name_btn)
        f.addRow("", QLabel(""))
        f.addRow("", self._calibrate_btn)
        f.addRow("", QLabel(""))
        f.addRow("", self._restart_btn)
        f.addRow("", QLabel(""))
        f.addRow("", self._factory_btn)

    async def _load(self) -> None:
        try:
            resp = await self._client.send_command("getDeviceName")
            if resp.is_ok and resp.data:
                name = resp.data.get("name") or ""
                self._name_edit.setText(name)
        except Exception:
            pass

    async def _on_set_name(self) -> None:
        name = self._name_edit.text().strip()
        if not name:
            self._toast("Name cannot be empty", error=True)
            return
        await self._run(f"setDeviceName:{name}", success_msg="Device name saved")

    async def _on_calibrate(self) -> None:
        await self._run("calibrateOrientation", success_msg="Calibration mode started on device")

    async def _on_restart(self) -> None:
        await self._run("reset", success_msg="Restarting…")

    def _on_factory_reset_clicked(self) -> None:
        box = QMessageBox(self)
        box.setWindowTitle("Factory Reset")
        box.setText(
            "This will erase ALL saved data (WiFi credentials, MQTT settings, "
            "pairing tokens) and restart the device.\n\nContinue?"
        )
        box.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel
        )
        box.setDefaultButton(QMessageBox.StandardButton.Cancel)
        if box.exec() == QMessageBox.StandardButton.Yes:
            asyncio.ensure_future(self._run("factoryReset", success_msg="Factory reset started…"))
