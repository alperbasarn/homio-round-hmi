"""Pairing wizard — 3-page QDialog for first-time device pairing.

Page 0 — Find device:    lists unpaired devices from DiscoveryService.
Page 1 — Get token:      instructs the user to open the portal and request a token.
Page 2 — Enter token:    4-char-grouped input; submits ``pair`` command to firmware.

On success the ``paired`` signal is emitted with the paired ``DeviceDescriptor``
and the dialog closes.  On ``error: unauth`` the error is shown inline on page 2
and the user can correct the token and retry.
"""

from __future__ import annotations

import asyncio
import logging
from typing import Callable

from PySide6.QtCore import Qt, Signal, Slot
from PySide6.QtWidgets import (
    QDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QPushButton,
    QSizePolicy,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from qnob_companion.discovery.descriptor import (
    Added,
    DeviceDescriptor,
    Removed,
    Updated,
)
from qnob_companion.discovery.service import DiscoveryService
from qnob_companion.pairing.secret_store import SecretStore
from qnob_companion.pairing.service import PairingResult, PairingService
from qnob_companion.transport.client import DeviceClient
from qnob_companion.transport.tcp import TcpTransport
from qnob_companion.transport.ble import BleTransport

log = logging.getLogger(__name__)

_TOKEN_LEN = 44  # 32 random bytes → urlsafe-base64 → 44 ASCII chars

# How to display the expected token format to the user.
_TOKEN_HINT = "e.g. XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX"


def _format_token_grouped(token: str) -> str:
    """Insert a space every 4 chars for human-readable display."""
    clean = token.replace(" ", "").replace("-", "")
    return " ".join(clean[i : i + 4] for i in range(0, len(clean), 4))


def _clean_token(raw: str) -> str:
    """Strip separators from a user-typed token."""
    return raw.replace(" ", "").replace("-", "").strip()


def _build_client_from_descriptor(desc: DeviceDescriptor) -> DeviceClient:
    """Construct a ``DeviceClient`` from a descriptor's available transports."""
    tcp: TcpTransport | None = None
    ble: BleTransport | None = None

    if desc.ip is not None and desc.tcp_port is not None:
        tcp = TcpTransport(desc.ip, desc.tcp_port)
    if desc.ble_address is not None:
        ble = BleTransport(desc.ble_address)

    if tcp is None and ble is None:
        raise ValueError(
            f"Device {desc.mac} has no reachable transport "
            "(no IP+port and no BLE address)"
        )
    return DeviceClient(tcp=tcp, ble=ble)


class PairingWizard(QDialog):
    """Modal 3-page dialog that guides the user through first-time pairing.

    Parameters
    ----------
    discovery:
        Already-started ``DiscoveryService`` whose event stream is consumed
        while the wizard is open to keep the device list fresh.
    secret_store:
        Token persistence backend (``KeyringSecretStore`` in production,
        ``MemorySecretStore`` in tests).
    parent:
        Optional Qt parent widget.
    client_factory:
        Optional callable ``(DeviceDescriptor) -> DeviceClient``.  Defaults to
        ``_build_client_from_descriptor`` which constructs TCP/BLE transports
        from the descriptor's fields.
    """

    #: Emitted after a successful pair — carries the paired DeviceDescriptor.
    paired: Signal = Signal(object)

    _PAGE_FIND = 0
    _PAGE_INSTRUCTIONS = 1
    _PAGE_TOKEN = 2

    def __init__(
        self,
        discovery: DiscoveryService,
        secret_store: SecretStore,
        parent: QWidget | None = None,
        client_factory: Callable[[DeviceDescriptor], DeviceClient] | None = None,
    ) -> None:
        super().__init__(parent)
        self._discovery = discovery
        self._secret_store = secret_store
        self._client_factory = client_factory or _build_client_from_descriptor

        self._selected: DeviceDescriptor | None = None
        self._client: DeviceClient | None = None
        self._pairing_svc: PairingService | None = None
        self._disc_task: asyncio.Task[None] | None = None

        self.setWindowTitle("Pair New Device")
        self.setMinimumSize(520, 400)
        self.setModal(True)

        self._build_ui()
        self._populate_device_list()

        # Start listening for discovery events to keep the list fresh.
        self._disc_task = asyncio.ensure_future(self._listen_discovery())

    # ------------------------------------------------------------------ UI build

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(16, 16, 16, 16)

        # Page stack
        self._stack = QStackedWidget()
        self._stack.addWidget(self._build_find_page())
        self._stack.addWidget(self._build_instructions_page())
        self._stack.addWidget(self._build_token_page())
        root.addWidget(self._stack, 1)

        # Bottom nav bar
        nav = QHBoxLayout()
        self._back_btn = QPushButton("← Back")
        self._back_btn.setVisible(False)
        self._back_btn.clicked.connect(self._on_back)

        self._next_btn = QPushButton("Next →")
        self._next_btn.setEnabled(False)
        self._next_btn.clicked.connect(self._on_next)

        cancel_btn = QPushButton("Cancel")
        cancel_btn.clicked.connect(self.reject)

        nav.addWidget(self._back_btn)
        nav.addStretch()
        nav.addWidget(cancel_btn)
        nav.addWidget(self._next_btn)
        root.addLayout(nav)

    def _build_find_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(
            QLabel("<h3>Step 1 of 3 — Find your device</h3>"),
        )
        layout.addWidget(
            QLabel(
                "Select the Qnob device you want to pair.\n"
                "Only devices not yet paired are shown."
            )
        )

        self._device_list = QListWidget()
        self._device_list.setAlternatingRowColors(True)
        self._device_list.currentItemChanged.connect(self._on_device_selected)
        layout.addWidget(self._device_list, 1)

        self._find_status = QLabel("")
        self._find_status.setStyleSheet("color: gray; font-style: italic;")
        layout.addWidget(self._find_status)
        return page

    def _build_instructions_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(QLabel("<h3>Step 2 of 3 — Request a token</h3>"))

        self._instr_label = QLabel()
        self._instr_label.setWordWrap(True)
        self._instr_label.setTextInteractionFlags(
            Qt.TextInteractionFlag.TextSelectableByMouse
        )
        layout.addWidget(self._instr_label)

        layout.addSpacing(12)
        layout.addWidget(
            QLabel(
                "Once the token is displayed on the device screen, click <b>Next</b>."
            )
        )
        layout.addStretch()
        return page

    def _build_token_page(self) -> QWidget:
        page = QWidget()
        layout = QVBoxLayout(page)
        layout.addWidget(QLabel("<h3>Step 3 of 3 — Enter the token</h3>"))
        layout.addWidget(
            QLabel(
                f"Type or paste the {_TOKEN_LEN}-character token shown on the device.\n"
                "Spaces between groups are optional."
            )
        )

        self._token_input = QLineEdit()
        self._token_input.setPlaceholderText(_TOKEN_HINT)
        self._token_input.setMaxLength(_TOKEN_LEN + 10)  # allow extra spaces
        self._token_input.setFont(self.font())
        self._token_input.setSizePolicy(
            QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Fixed
        )
        self._token_input.textChanged.connect(self._on_token_changed)
        layout.addWidget(self._token_input)

        self._token_error = QLabel("")
        self._token_error.setStyleSheet("color: #c0392b; font-weight: bold;")
        self._token_error.setWordWrap(True)
        layout.addWidget(self._token_error)

        self._pair_btn = QPushButton("Pair")
        self._pair_btn.setEnabled(False)
        self._pair_btn.clicked.connect(self._on_pair_clicked)
        layout.addWidget(self._pair_btn, alignment=Qt.AlignmentFlag.AlignRight)

        layout.addStretch()
        return page

    # ------------------------------------------------------------------ page transitions

    def _go_to(self, page: int) -> None:
        self._stack.setCurrentIndex(page)
        self._back_btn.setVisible(page > 0)

        if page == self._PAGE_FIND:
            self._next_btn.setText("Next →")
            self._next_btn.setEnabled(self._selected is not None)
            self._next_btn.setVisible(True)
        elif page == self._PAGE_INSTRUCTIONS:
            self._next_btn.setText("Next →")
            self._next_btn.setEnabled(True)
            self._next_btn.setVisible(True)
        elif page == self._PAGE_TOKEN:
            self._next_btn.setVisible(False)
            self._token_input.setFocus()

    @Slot()
    def _on_back(self) -> None:
        current = self._stack.currentIndex()
        if current > 0:
            self._go_to(current - 1)

    @Slot()
    def _on_next(self) -> None:
        current = self._stack.currentIndex()
        if current == self._PAGE_FIND:
            if self._selected is None:
                return
            # Build client + pairing service for the selected device.
            try:
                self._client = self._client_factory(self._selected)
            except ValueError as exc:
                self._find_status.setText(str(exc))
                return
            self._pairing_svc = PairingService(
                self._client, self._secret_store, self._selected.mac
            )
            # Populate instructions for this device.
            name = self._selected.name or self._selected.mac
            self._instr_label.setText(
                f"1. On your computer or phone, open:\n\n"
                f"   <b>http://{name}.local/</b>\n\n"
                f"2. On the portal page, press <b>\"Pair PC App\"</b>.\n\n"
                f"3. A {_TOKEN_LEN}-character token will appear on the device screen."
            )
            # Start the TCP connection in the background.
            asyncio.ensure_future(self._connect_client())
            self._go_to(self._PAGE_INSTRUCTIONS)

        elif current == self._PAGE_INSTRUCTIONS:
            self._token_error.setText("")
            self._go_to(self._PAGE_TOKEN)

    # ------------------------------------------------------------------ device list

    def _populate_device_list(self) -> None:
        self._device_list.clear()
        snapshot = {
            mac: desc
            for mac, desc in self._discovery.devices.items()
            if not desc.is_paired
        }
        for desc in snapshot.values():
            self._add_list_item(desc)

        if not snapshot:
            self._find_status.setText("Scanning for unpaired devices…")
        else:
            self._find_status.setText("")

    def _add_list_item(self, desc: DeviceDescriptor) -> None:
        label = desc.name or desc.mac
        detail_parts: list[str] = [desc.mac]
        if desc.ip is not None:
            detail_parts.append(str(desc.ip))
        if desc.rssi_dbm is not None:
            detail_parts.append(f"{desc.rssi_dbm} dBm")
        item = QListWidgetItem(f"{label}  ({', '.join(detail_parts)})")
        item.setData(Qt.ItemDataRole.UserRole, desc)
        self._device_list.addItem(item)

    @Slot()
    def _on_device_selected(self) -> None:
        item = self._device_list.currentItem()
        if item is not None:
            self._selected = item.data(Qt.ItemDataRole.UserRole)
            self._next_btn.setEnabled(True)
            self._find_status.setText("")
        else:
            self._selected = None
            self._next_btn.setEnabled(False)

    # ------------------------------------------------------------------ token page

    @Slot()
    def _on_token_changed(self) -> None:
        clean = _clean_token(self._token_input.text())
        self._pair_btn.setEnabled(len(clean) == _TOKEN_LEN)
        self._token_error.setText("")

    @Slot()
    def _on_pair_clicked(self) -> None:
        token = _clean_token(self._token_input.text())
        if len(token) != _TOKEN_LEN:
            self._token_error.setText(
                f"Token must be {_TOKEN_LEN} characters (got {len(token)})."
            )
            return
        self._set_pairing_in_progress(True)
        asyncio.ensure_future(self._do_pair(token))

    def _set_pairing_in_progress(self, busy: bool) -> None:
        self._pair_btn.setEnabled(not busy)
        self._token_input.setEnabled(not busy)
        self._back_btn.setEnabled(not busy)
        if busy:
            self._pair_btn.setText("Pairing…")
            self._token_error.setText("")
        else:
            self._pair_btn.setText("Pair")

    # ------------------------------------------------------------------ async work

    async def _connect_client(self) -> None:
        if self._client is None:
            return
        try:
            await self._client.connect()
        except Exception:  # noqa: BLE001
            log.exception("Client connect failed for %s", self._selected)

    async def _do_pair(self, token: str) -> None:
        if self._pairing_svc is None:
            return
        try:
            result = await self._pairing_svc.pair(token)
        except Exception:  # noqa: BLE001
            log.exception("Unexpected error during pair for %s", self._selected)
            self._set_pairing_in_progress(False)
            self._token_error.setText("An unexpected error occurred. Please try again.")
            return

        self._set_pairing_in_progress(False)

        if result == PairingResult.OK:
            log.info("Pairing wizard: paired %s", self._selected)
            self.paired.emit(self._selected)
            self.accept()
        elif result == PairingResult.WRONG_TOKEN:
            self._token_error.setText(
                "Wrong token. Check the device screen and try again."
            )
            self._token_input.selectAll()
            self._token_input.setFocus()
        else:
            self._token_error.setText(
                "Could not reach the device. Make sure it is connected to the same "
                "network and try again."
            )

    async def _listen_discovery(self) -> None:
        """Background task: consume DiscoveryService events and refresh the list."""
        try:
            async for event in self._discovery:
                if isinstance(event, (Added, Updated)):
                    if not event.device.is_paired:
                        self._populate_device_list()
                    elif event.device.is_paired:
                        # Device appeared as already paired — refresh anyway.
                        self._populate_device_list()
                elif isinstance(event, Removed):
                    self._populate_device_list()
        except asyncio.CancelledError:
            pass

    # ------------------------------------------------------------------ cleanup

    def closeEvent(self, event: object) -> None:  # type: ignore[override]
        if self._disc_task is not None and not self._disc_task.done():
            self._disc_task.cancel()
        super().closeEvent(event)  # type: ignore[misc]
