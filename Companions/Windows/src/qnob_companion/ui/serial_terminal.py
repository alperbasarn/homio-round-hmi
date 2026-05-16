"""Serial terminal widget — reusable COM-port terminal for the companion app.

Used as:
- A standalone main-window page (``SerialTerminalPage``)
- A tab inside ``DeviceDetailDialog`` (``SerialTab`` wraps it)
"""

from __future__ import annotations

import asyncio
import logging

from PySide6.QtCore import QTimer, Slot
from PySide6.QtGui import QFont
from PySide6.QtWidgets import (
    QComboBox,
    QFrame,
    QGroupBox,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QSizePolicy,
    QVBoxLayout,
    QWidget,
)

from qnob_companion.transport.serial import (
    DEFAULT_BAUDRATE,
    SerialTransport,
    list_serial_ports,
)
from qnob_companion.transport.base import TransportError, TransportState

log = logging.getLogger(__name__)

_BAUDRATES = [9600, 57600, 115200, 230400, 460800, 921600]
_POLL_MS = 50  # how often to drain raw_lines into the text area


class SerialTerminalWidget(QWidget):
    """Full serial terminal: port/baud picker, raw output display, command input.

    Each instance owns its own ``SerialTransport``.  Multiple instances on
    different ports can coexist; one instance per physical port is the natural
    limit imposed by the OS.
    """

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._transport: SerialTransport | None = None
        self._poll_timer = QTimer(self)
        self._poll_timer.setInterval(_POLL_MS)
        self._poll_timer.timeout.connect(self._drain_raw_lines)

        self._build_ui()
        self._refresh_ports()

    # ------------------------------------------------------------------ UI

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(6)

        # ── Connection controls ──────────────────────────────────────────
        ctrl = QHBoxLayout()

        ctrl.addWidget(QLabel("Port:"))
        self._port_combo = QComboBox()
        self._port_combo.setMinimumWidth(100)
        ctrl.addWidget(self._port_combo)

        self._refresh_btn = QPushButton("⟳")
        self._refresh_btn.setFixedWidth(30)
        self._refresh_btn.setToolTip("Refresh port list")
        self._refresh_btn.clicked.connect(self._refresh_ports)
        ctrl.addWidget(self._refresh_btn)

        ctrl.addSpacing(8)
        ctrl.addWidget(QLabel("Baud:"))
        self._baud_combo = QComboBox()
        for rate in _BAUDRATES:
            self._baud_combo.addItem(str(rate), rate)
        self._baud_combo.setCurrentText(str(DEFAULT_BAUDRATE))
        self._baud_combo.setFixedWidth(90)
        ctrl.addWidget(self._baud_combo)

        ctrl.addSpacing(8)
        self._connect_btn = QPushButton("Connect")
        self._connect_btn.setFixedWidth(90)
        self._connect_btn.clicked.connect(self._on_connect_clicked)
        ctrl.addWidget(self._connect_btn)

        self._status_label = QLabel("Not connected")
        self._status_label.setStyleSheet("color: gray; font-style: italic;")
        ctrl.addWidget(self._status_label, 1)

        root.addLayout(ctrl)

        # ── Quick-action buttons ─────────────────────────────────────────
        quick = QHBoxLayout()
        for label, cmd in [
            ("Ping", "ping"),
            ("Status", "status"),
            ("Help", "help"),
            ("Unpair", "unpair"),
        ]:
            btn = QPushButton(label)
            btn.setFixedWidth(72)
            btn.setEnabled(False)
            btn.setProperty("quick_cmd", cmd)
            btn.clicked.connect(self._on_quick_clicked)
            quick.addWidget(btn)
            setattr(self, f"_quick_{label.lower()}", btn)
        quick.addStretch()

        self._clear_btn = QPushButton("Clear")
        self._clear_btn.setFixedWidth(60)
        self._clear_btn.clicked.connect(self._on_clear)
        quick.addWidget(self._clear_btn)
        root.addLayout(quick)

        # ── Output area ──────────────────────────────────────────────────
        self._output = QPlainTextEdit()
        self._output.setReadOnly(True)
        mono = QFont("Consolas", 9)
        mono.setStyleHint(QFont.StyleHint.Monospace)
        self._output.setFont(mono)
        self._output.setMaximumBlockCount(5000)
        root.addWidget(self._output, 1)

        # ── Command input ────────────────────────────────────────────────
        cmd_row = QHBoxLayout()
        self._cmd_input = QLineEdit()
        self._cmd_input.setPlaceholderText("Type a command and press Enter or Send…")
        self._cmd_input.setFont(mono)
        self._cmd_input.returnPressed.connect(self._on_send)
        self._cmd_input.setEnabled(False)
        cmd_row.addWidget(self._cmd_input, 1)

        self._send_btn = QPushButton("Send")
        self._send_btn.setEnabled(False)
        self._send_btn.clicked.connect(self._on_send)
        cmd_row.addWidget(self._send_btn)
        root.addLayout(cmd_row)

    # ------------------------------------------------------------------ port helpers

    @Slot()
    def _refresh_ports(self) -> None:
        current = self._port_combo.currentText()
        self._port_combo.clear()
        ports = list_serial_ports()
        for p in ports:
            self._port_combo.addItem(p)
        if current in ports:
            self._port_combo.setCurrentText(current)

    def _set_connected(self, connected: bool) -> None:
        self._connect_btn.setText("Disconnect" if connected else "Connect")
        self._port_combo.setEnabled(not connected)
        self._baud_combo.setEnabled(not connected)
        self._refresh_btn.setEnabled(not connected)
        self._cmd_input.setEnabled(connected)
        self._send_btn.setEnabled(connected)
        for label in ("ping", "status", "help", "unpair"):
            btn = getattr(self, f"_quick_{label}", None)
            if btn is not None:
                btn.setEnabled(connected)
        if connected:
            self._status_label.setStyleSheet("color: #27ae60; font-weight: bold;")
        else:
            self._status_label.setStyleSheet("color: gray; font-style: italic;")

    def _append(self, text: str) -> None:
        self._output.appendPlainText(text)

    # ------------------------------------------------------------------ slots

    @Slot()
    def _on_connect_clicked(self) -> None:
        if self._transport is not None and self._transport.state == TransportState.CONNECTED:
            asyncio.ensure_future(self._do_disconnect())
        else:
            port = self._port_combo.currentText()
            if not port:
                self._status_label.setText("No port selected")
                return
            baud = self._baud_combo.currentData() or DEFAULT_BAUDRATE
            asyncio.ensure_future(self._do_connect(port, baud))

    @Slot()
    def _on_send(self) -> None:
        text = self._cmd_input.text().strip()
        if not text:
            return
        self._cmd_input.clear()
        asyncio.ensure_future(self._do_send_raw(text))

    @Slot()
    def _on_quick_clicked(self) -> None:
        sender = self.sender()
        cmd = sender.property("quick_cmd") if sender else None
        if cmd:
            asyncio.ensure_future(self._do_send_raw(cmd))

    @Slot()
    def _on_clear(self) -> None:
        self._output.clear()

    @Slot()
    def _drain_raw_lines(self) -> None:
        if self._transport is None:
            return
        q = self._transport.raw_lines
        lines: list[str] = []
        try:
            while True:
                lines.append(q.get_nowait())
        except asyncio.QueueEmpty:
            pass
        if lines:
            self._output.appendPlainText("\n".join(lines))
            # Auto-scroll to bottom.
            sb = self._output.verticalScrollBar()
            sb.setValue(sb.maximum())

    # ------------------------------------------------------------------ async work

    async def _do_connect(self, port: str, baud: int) -> None:
        self._connect_btn.setEnabled(False)
        self._status_label.setText(f"Connecting to {port}…")
        self._transport = SerialTransport(port, baud)
        try:
            await self._transport.connect()
        except TransportError as exc:
            self._status_label.setText(f"Error: {exc}")
            self._status_label.setStyleSheet("color: #c0392b;")
            self._connect_btn.setEnabled(True)
            self._transport = None
            return
        self._set_connected(True)
        self._status_label.setText(f"Connected — {port} @ {baud}")
        self._poll_timer.start()
        self._append(f"[connected to {port}]")

    async def _do_disconnect(self) -> None:
        self._connect_btn.setEnabled(False)
        self._poll_timer.stop()
        if self._transport is not None:
            await self._transport.disconnect()
            self._transport = None
        self._set_connected(False)
        self._status_label.setText("Disconnected")
        self._append("[disconnected]")
        self._connect_btn.setEnabled(True)

    async def _do_send_raw(self, text: str) -> None:
        if self._transport is None or self._transport.state != TransportState.CONNECTED:
            return
        self._append(f"> {text}")
        try:
            await self._transport.write_raw(text)
        except TransportError as exc:
            self._append(f"[send error: {exc}]")

    # ------------------------------------------------------------------ cleanup

    def closeEvent(self, event: object) -> None:  # type: ignore[override]
        self._poll_timer.stop()
        if self._transport is not None and self._transport.state == TransportState.CONNECTED:
            asyncio.ensure_future(self._transport.disconnect())
        super().closeEvent(event)  # type: ignore[misc]


class SerialTerminalPage(QWidget):
    """Standalone main-window page wrapping ``SerialTerminalWidget``."""

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)

        header = QLabel("<h2>Serial Terminal</h2>")
        header.setContentsMargins(12, 8, 12, 0)
        layout.addWidget(header)

        sep = QFrame()
        sep.setFrameShape(QFrame.Shape.HLine)
        sep.setStyleSheet("color: #ccc;")
        layout.addWidget(sep)

        self._terminal = SerialTerminalWidget()
        layout.addWidget(self._terminal, 1)

    def closeEvent(self, event: object) -> None:  # type: ignore[override]
        self._terminal.closeEvent(event)
