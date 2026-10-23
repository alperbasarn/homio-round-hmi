"""MQTT broker configuration tab."""

from __future__ import annotations

import asyncio

from PySide6.QtWidgets import QLabel, QLineEdit, QPushButton, QWidget

from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.base_tab import BaseTab


class MqttTab(BaseTab):
    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        self._host_edit = QLineEdit()
        self._host_edit.setPlaceholderText("broker.example.com or IP")
        self._port_edit = QLineEdit("8883")
        self._user_edit = QLineEdit()
        self._pass_edit = QLineEdit()
        self._pass_edit.setEchoMode(QLineEdit.EchoMode.Password)
        self._status_lbl = QLabel("—")
        self._apply_btn = QPushButton("Apply MQTT Settings")
        super().__init__(client, parent)
        self._apply_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_apply()))

    def _build_form(self) -> None:
        f = self._form
        f.addRow("Connection", self._status_lbl)
        f.addRow("Broker host", self._host_edit)
        f.addRow("Port", self._port_edit)
        f.addRow("Username", self._user_edit)
        f.addRow("Password", self._pass_edit)
        f.addRow("", self._apply_btn)

    async def _load(self) -> None:
        data = await self._fetch_status()
        if data and "mqtt" in data:
            connected = data["mqtt"].get("connected", False)
            self._status_lbl.setText("Connected" if connected else "Disconnected")

    async def _on_apply(self) -> None:
        host = self._host_edit.text().strip()
        port = self._port_edit.text().strip() or "8883"
        user = self._user_edit.text().strip()
        pwd  = self._pass_edit.text()
        if not host:
            self._toast("Broker host is required", error=True)
            return
        # Legacy format: "configureMQTTServer:host:port:user:pass"
        parts = [host, port]
        if user or pwd:
            parts += [user, pwd]
        cmd = "configureMQTTServer:" + ":".join(parts)
        await self._run(cmd, success_msg="MQTT configured — reconnecting…")
