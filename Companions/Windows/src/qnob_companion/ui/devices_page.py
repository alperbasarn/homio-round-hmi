"""C1 — Device dashboard: live list of discovered Qnob devices."""

from __future__ import annotations

import asyncio
import logging
from typing import TYPE_CHECKING

from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtGui import QColor, QPainter
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QSizePolicy,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from qnob_companion.discovery.descriptor import DeviceDescriptor
from qnob_companion.discovery.service import DiscoveryService
from qnob_companion.pairing.secret_store import SecretStore

if TYPE_CHECKING:
    pass

log = logging.getLogger(__name__)


# ---------------------------------------------------------------------------
# Transport badge widget
# ---------------------------------------------------------------------------

class _Badge(QLabel):
    """Tiny coloured pill showing a transport type."""

    _COLOURS: dict[str, tuple[str, str]] = {
        "tcp": ("#1a7f4b", "#d4f0e3"),   # dark-green on light-green
        "ble": ("#1a4f7f", "#d4e8f0"),   # dark-blue on light-blue
    }

    def __init__(self, label: str, parent: QWidget | None = None) -> None:
        super().__init__(label.upper(), parent)
        fg, bg = self._COLOURS.get(label.lower(), ("#555", "#eee"))
        self.setStyleSheet(
            f"QLabel {{ background: {bg}; color: {fg}; border-radius: 3px;"
            f"  padding: 1px 5px; font-size: 10px; font-weight: bold; }}"
        )
        self.setSizePolicy(QSizePolicy.Policy.Fixed, QSizePolicy.Policy.Fixed)


# ---------------------------------------------------------------------------
# Status dot widget (online / offline)
# ---------------------------------------------------------------------------

class _StatusDot(QWidget):
    _ONLINE_COLOR = QColor("#22c55e")   # green-500
    _OFFLINE_COLOR = QColor("#9ca3af")  # gray-400

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self.setFixedSize(10, 10)
        self._online = False

    def set_online(self, online: bool) -> None:
        if self._online != online:
            self._online = online
            self.update()

    def paintEvent(self, _event: object) -> None:  # type: ignore[override]
        painter = QPainter(self)
        painter.setRenderHint(QPainter.RenderHint.Antialiasing)
        color = self._ONLINE_COLOR if self._online else self._OFFLINE_COLOR
        painter.setBrush(color)
        painter.setPen(Qt.PenStyle.NoPen)
        painter.drawEllipse(0, 0, 10, 10)


# ---------------------------------------------------------------------------
# Device row widget
# ---------------------------------------------------------------------------

class _DeviceRow(QWidget):
    """One row in the device list.

    Emits ``pair_requested`` or ``open_requested`` when the action button
    is clicked; parent widget connects those signals.
    """

    pair_requested: Signal = Signal(object)   # DeviceDescriptor
    open_requested: Signal = Signal(object)   # DeviceDescriptor

    def __init__(
        self,
        device: DeviceDescriptor,
        is_locally_paired: bool,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._device = device
        self.setMinimumHeight(56)

        outer = QHBoxLayout(self)
        outer.setContentsMargins(12, 8, 12, 8)
        outer.setSpacing(10)

        self._dot = _StatusDot()
        self._dot.set_online(True)  # visible in discovery → online
        outer.addWidget(self._dot, 0, Qt.AlignmentFlag.AlignVCenter)

        # Name + badges column
        info = QVBoxLayout()
        info.setSpacing(2)

        name_row = QHBoxLayout()
        name_row.setSpacing(6)
        self._name_lbl = QLabel(device.name or device.mac)
        font = self._name_lbl.font()
        font.setPointSize(font.pointSize() + 1)
        font.setBold(True)
        self._name_lbl.setFont(font)
        name_row.addWidget(self._name_lbl)

        self._badges: list[_Badge] = []
        for t in sorted(device.transports):
            badge = _Badge(t)
            self._badges.append(badge)
            name_row.addWidget(badge)
        name_row.addStretch()
        info.addLayout(name_row)

        self._sub_lbl = QLabel(self._sub_text(device))
        self._sub_lbl.setStyleSheet("color: #888; font-size: 11px;")
        info.addWidget(self._sub_lbl)

        outer.addLayout(info, 1)

        # Action button
        self._btn = QPushButton()
        self._btn.setFixedWidth(70)
        self._btn.clicked.connect(self._on_button)
        outer.addWidget(self._btn, 0, Qt.AlignmentFlag.AlignVCenter)

        self._is_locally_paired = is_locally_paired
        self._refresh_button()

    # ----- public update API -----

    def update_device(self, device: DeviceDescriptor, is_locally_paired: bool) -> None:
        self._device = device
        self._is_locally_paired = is_locally_paired
        self._name_lbl.setText(device.name or device.mac)
        self._sub_lbl.setText(self._sub_text(device))

        # Rebuild badges only if transports changed.
        current_labels = {b.text().lower() for b in self._badges}
        if current_labels != device.transports:
            for b in self._badges:
                b.setParent(None)  # type: ignore[arg-type]
            self._badges.clear()
            name_row: QHBoxLayout = self.layout().itemAt(1).layout().itemAt(0).layout()  # type: ignore[union-attr]
            for t in sorted(device.transports):
                badge = _Badge(t)
                self._badges.append(badge)
                name_row.insertWidget(name_row.count() - 1, badge)

        self._dot.set_online(True)
        self._refresh_button()

    def mark_offline(self) -> None:
        self._dot.set_online(False)
        self._btn.setEnabled(False)

    # ----- internals -----

    @staticmethod
    def _sub_text(device: DeviceDescriptor) -> str:
        parts: list[str] = []
        if device.firmware_version:
            parts.append(f"fw {device.firmware_version}")
        if device.ip:
            parts.append(str(device.ip))
        if device.rssi_dbm is not None:
            parts.append(f"{device.rssi_dbm} dBm")
        return "  ·  ".join(parts) if parts else device.mac

    def _refresh_button(self) -> None:
        if self._is_locally_paired:
            self._btn.setText("Open")
            self._btn.setStyleSheet("")
        else:
            self._btn.setText("Pair")
            self._btn.setStyleSheet(
                "QPushButton { color: #1a7f4b; border: 1px solid #1a7f4b;"
                " border-radius: 4px; padding: 3px 6px; }"
            )
        self._btn.setEnabled(True)

    @Slot()
    def _on_button(self) -> None:
        if self._is_locally_paired:
            self.open_requested.emit(self._device)
        else:
            self.pair_requested.emit(self._device)


# ---------------------------------------------------------------------------
# Devices page
# ---------------------------------------------------------------------------

class DevicesPage(QWidget):
    """Live device list — the real dashboard replacing the B4 placeholder.

    Consumes ``DiscoveryService`` events on the asyncio loop and marshals
    them to the Qt main thread (safe because qasync runs asyncio on the
    same thread as Qt).
    """

    def __init__(
        self,
        discovery: DiscoveryService,
        secret_store: SecretStore,
        parent: QWidget | None = None,
    ) -> None:
        super().__init__(parent)
        self._discovery = discovery
        self._store = secret_store
        self._task: asyncio.Task[None] | None = None
        self._rows: dict[str, tuple[QListWidgetItem, _DeviceRow]] = {}

        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)

        # ── Header bar ────────────────────────────────────────────────────
        header = QHBoxLayout()
        header.setContentsMargins(16, 12, 16, 8)
        title = QLabel("Devices")
        font = title.font()
        font.setPointSize(font.pointSize() + 4)
        font.setBold(True)
        title.setFont(font)
        header.addWidget(title)
        header.addStretch()

        self._refresh_btn = QPushButton("⟳  Refresh")
        self._refresh_btn.clicked.connect(self._on_refresh)
        header.addWidget(self._refresh_btn)

        pair_btn = QPushButton("＋  Pair New…")
        pair_btn.clicked.connect(self._on_pair_new)
        header.addWidget(pair_btn)

        root.addLayout(header)

        # ── Content: empty-state ↔ device list ──────────────────────────
        self._stack = QStackedWidget()

        self._empty_widget = self._build_empty_widget()
        self._stack.addWidget(self._empty_widget)

        self._list = QListWidget()
        self._list.setSpacing(2)
        self._list.setFrameShape(QListWidget.Shape.NoFrame)
        self._stack.addWidget(self._list)

        self._stack.setCurrentIndex(0)  # start with empty state
        root.addWidget(self._stack, 1)

    # ----- lifecycle -----

    def start(self) -> None:
        """Start consuming discovery events. Call after asyncio loop is running."""
        if self._task is None or self._task.done():
            self._task = asyncio.ensure_future(
                self._discovery_loop(), loop=asyncio.get_event_loop()
            )
            log.debug("DevicesPage discovery task started")

    def stop(self) -> None:
        if self._task is not None and not self._task.done():
            self._task.cancel()
            self._task = None

    # ----- asyncio event consumer -----

    async def _discovery_loop(self) -> None:
        from qnob_companion.discovery.descriptor import Added, Removed, Updated

        try:
            async for event in self._discovery:
                if isinstance(event, Added):
                    self._add_device(event.device)
                elif isinstance(event, Updated):
                    self._update_device(event.device)
                elif isinstance(event, Removed):
                    self._remove_device(event.mac)
        except asyncio.CancelledError:
            raise
        except Exception:
            log.exception("DevicesPage discovery loop crashed")

    # ----- list manipulation (called from asyncio/Qt main thread) -----

    def _add_device(self, device: DeviceDescriptor) -> None:
        if device.mac in self._rows:
            self._update_device(device)
            return

        is_paired = self._store.get(device.mac) is not None
        row_widget = _DeviceRow(device, is_paired)
        row_widget.pair_requested.connect(self._on_pair_requested)
        row_widget.open_requested.connect(self._on_open_requested)

        item = QListWidgetItem(self._list)
        item.setSizeHint(row_widget.sizeHint())
        self._list.setItemWidget(item, row_widget)

        self._rows[device.mac] = (item, row_widget)
        self._show_list()
        log.debug("Added device %s (%s)", device.name or device.mac, device.mac)

    def _update_device(self, device: DeviceDescriptor) -> None:
        entry = self._rows.get(device.mac)
        if entry is None:
            self._add_device(device)
            return
        item, row = entry
        is_paired = self._store.get(device.mac) is not None
        row.update_device(device, is_paired)
        item.setSizeHint(row.sizeHint())

    def _remove_device(self, mac: str) -> None:
        entry = self._rows.pop(mac, None)
        if entry is None:
            return
        item, row = entry
        row.mark_offline()
        # Remove from list widget.
        row_idx = self._list.row(item)
        self._list.takeItem(row_idx)
        log.debug("Removed device %s", mac)
        if not self._rows:
            self._show_empty()

    # ----- slot handlers -----

    @Slot(object)
    def _on_pair_requested(self, device: DeviceDescriptor) -> None:
        from qnob_companion.ui.pairing_wizard import PairingWizard

        wizard = PairingWizard(self._discovery, self._store, parent=self)
        wizard.paired.connect(self._on_device_paired)
        wizard.exec()

    @Slot(object)
    def _on_open_requested(self, device: DeviceDescriptor) -> None:
        # Device detail page lands in C2.
        log.info("Open requested for %s — device detail page lands in #67 C2", device.mac)

    @Slot(object)
    def _on_device_paired(self, device: DeviceDescriptor) -> None:
        # Refresh the row for the newly paired device.
        self._update_device(device)

    @Slot()
    def _on_refresh(self) -> None:
        # Re-seed from what the discovery service already knows.
        snapshot = self._discovery.devices
        for mac, device in snapshot.items():
            if mac in self._rows:
                self._update_device(device)
            else:
                self._add_device(device)

    @Slot()
    def _on_pair_new(self) -> None:
        from qnob_companion.ui.pairing_wizard import PairingWizard

        wizard = PairingWizard(self._discovery, self._store, parent=self)
        wizard.paired.connect(self._on_device_paired)
        wizard.exec()

    # ----- internal helpers -----

    def _show_list(self) -> None:
        self._stack.setCurrentWidget(self._list)

    def _show_empty(self) -> None:
        self._stack.setCurrentWidget(self._empty_widget)

    @staticmethod
    def _build_empty_widget() -> QWidget:
        w = QWidget()
        layout = QVBoxLayout(w)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(
            QLabel(
                "No Qnob devices found.\n\n"
                "Make sure your device is connected to the same WiFi network,\n"
                "or is in BLE range.",
                alignment=Qt.AlignmentFlag.AlignCenter,
            )
        )
        return w
