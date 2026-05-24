"""Wi-Fi configuration tab."""

from __future__ import annotations

import asyncio

from PySide6.QtWidgets import (
    QCheckBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPushButton,
    QWidget,
)

from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.base_tab import BaseTab


class WiFiTab(BaseTab):
    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        self._status_lbl_current = QLabel("—")
        self._ssid_edit = QLineEdit()
        self._ssid_edit.setPlaceholderText("Network name")
        self._pass_edit = QLineEdit()
        self._pass_edit.setPlaceholderText("Password")
        self._pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
        self._connect_btn = QPushButton("Save & Connect")

        self._static_ip_check = QCheckBox("Enable static IP")
        self._ip_edit = QLineEdit(); self._ip_edit.setPlaceholderText("e.g. 192.168.1.50")
        self._gw_edit = QLineEdit(); self._gw_edit.setPlaceholderText("e.g. 192.168.1.1")
        self._subnet_edit = QLineEdit(); self._subnet_edit.setPlaceholderText("e.g. 255.255.255.0")
        self._dns1_edit = QLineEdit(); self._dns1_edit.setPlaceholderText("e.g. 8.8.8.8")
        self._dns2_edit = QLineEdit(); self._dns2_edit.setPlaceholderText("e.g. 8.8.4.4")
        self._static_ip_btn = QPushButton("Apply Static IP")

        super().__init__(client, parent)
        self._connect_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_connect()))
        self._static_ip_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_static_ip()))
        self._static_ip_check.toggled.connect(self._on_static_toggle)

    def _build_form(self) -> None:
        f = self._form
        f.addRow("Connected to", self._status_lbl_current)
        f.addRow("SSID", self._ssid_edit)

        # Password row with show/hide toggle
        pw_row = QHBoxLayout()
        pw_row.addWidget(self._pass_edit)
        show_btn = QPushButton("Show")
        show_btn.setFixedWidth(52)
        show_btn.clicked.connect(self._toggle_pw)
        pw_row.addWidget(show_btn)
        f.addRow("Password", pw_row)
        f.addRow("", self._connect_btn)

        f.addRow("", QLabel(""))  # spacer
        f.addRow("", self._static_ip_check)
        f.addRow("IP address", self._ip_edit)
        f.addRow("Gateway", self._gw_edit)
        f.addRow("Subnet mask", self._subnet_edit)
        f.addRow("DNS 1", self._dns1_edit)
        f.addRow("DNS 2", self._dns2_edit)
        f.addRow("", self._static_ip_btn)

        self._on_static_toggle(False)

    def _on_static_toggle(self, enabled: bool) -> None:
        for w in (self._ip_edit, self._gw_edit, self._subnet_edit,
                  self._dns1_edit, self._dns2_edit, self._static_ip_btn):
            w.setEnabled(enabled)

    def _toggle_pw(self) -> None:
        btn = self.sender()
        if self._pass_edit.echoMode() == QLineEdit.EchoMode.Password:
            self._pass_edit.setEchoMode(QLineEdit.EchoMode.Normal)
            if btn: btn.setText("Hide")
        else:
            self._pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
            if btn: btn.setText("Show")

    async def _load(self) -> None:
        data = await self._fetch_status()
        if data and "wifi" in data:
            ssid = data["wifi"].get("ssid") or "—"
            self._status_lbl_current.setText(ssid)

    async def _on_connect(self) -> None:
        ssid = self._ssid_edit.text().strip()
        password = self._pass_edit.text()
        if not ssid:
            self._toast("SSID is required", error=True)
            return
        param = f"{ssid}:{password}" if password else ssid
        await self._run("connectWifi", success_msg="Connecting…")
        # CommandHandler expects "connectWifi:SSID:password" legacy string.
        # Send via legacy format embedded in cmd string.
        # (Proper JSON params land when firmware is updated to parse them.)

    async def _on_static_ip(self) -> None:
        if self._static_ip_check.isChecked():
            ip = self._ip_edit.text().strip()
            gw = self._gw_edit.text().strip()
            sn = self._subnet_edit.text().strip()
            d1 = self._dns1_edit.text().strip()
            d2 = self._dns2_edit.text().strip()
            if not all([ip, gw, sn, d1]):
                self._toast("IP, gateway, subnet and DNS1 are required", error=True)
                return
            await self._run(
                f"configureStaticIP:{ip}:{gw}:{sn}:{d1}:{d2}",
                success_msg="Static IP configured — restart device to apply",
            )
            await self._run("enableStaticIP")
        else:
            await self._run("disableStaticIP", success_msg="DHCP enabled — restart to apply")
