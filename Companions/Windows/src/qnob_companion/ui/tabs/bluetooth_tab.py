"""Bluetooth configuration tab."""

from __future__ import annotations

import asyncio

from PySide6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QPushButton,
    QWidget,
)

from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.base_tab import BaseTab


class BluetoothTab(BaseTab):
    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        self._enabled_check = QCheckBox("Bluetooth enabled")
        self._enabled_check.setChecked(True)
        self._name_edit = QLineEdit()
        self._name_edit.setPlaceholderText("Qnob PC Control")
        self._name_btn = QPushButton("Set Name")
        self._toggle_btn = QPushButton("Apply Enable/Disable")

        self._bonded_list = QListWidget()
        self._bonded_list.setMaximumHeight(120)
        self._scan_btn = QPushButton("Scan BLE Devices (5 s)")
        self._clear_btn = QPushButton("Clear All Bonds")

        self._status_lbl2 = QLabel("—")
        super().__init__(client, parent)
        self._name_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_set_name()))
        self._toggle_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_toggle()))
        self._scan_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_scan()))
        self._clear_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_clear_bonds()))

    def _build_form(self) -> None:
        f = self._form
        f.addRow("Status", self._status_lbl2)
        f.addRow("", self._enabled_check)
        f.addRow("", self._toggle_btn)
        f.addRow("Device name", self._name_edit)
        f.addRow("", QLabel("Name change takes effect after device restart."))
        f.addRow("", self._name_btn)

        row = QHBoxLayout()
        row.addWidget(self._scan_btn)
        row.addWidget(self._clear_btn)
        f.addRow("Actions", row)
        f.addRow("Bonded devices", self._bonded_list)

    async def _load(self) -> None:
        data = await self._fetch_status()
        if data and "ble" in data:
            clients = data["ble"].get("connected_clients", 0)
            self._status_lbl2.setText(
                f"Enabled · {clients} client(s) connected" if clients else "Enabled · idle"
            )

    async def _on_set_name(self) -> None:
        name = self._name_edit.text().strip()
        if not name:
            self._toast("Name cannot be empty", error=True)
            return
        await self._run(f"setBluetoothName:{name}", success_msg="Name saved — restart to apply")

    async def _on_toggle(self) -> None:
        state = "on" if self._enabled_check.isChecked() else "off"
        await self._run(f"setBluetooth:{state}", success_msg=f"Bluetooth {state}")

    async def _on_scan(self) -> None:
        self._bonded_list.clear()
        self._bonded_list.addItem("Scanning…")
        await self._run("startBtScan:5", success_msg="Scan started — results in 5 s")

    async def _on_clear_bonds(self) -> None:
        await self._run("clearBtBonds", success_msg="All bonds cleared")
        self._bonded_list.clear()
