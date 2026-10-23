"""Serial tab — thin wrapper around SerialTerminalWidget for use in DeviceDetailDialog."""

from __future__ import annotations

from PySide6.QtWidgets import QVBoxLayout, QWidget

from qnob_companion.ui.serial_terminal import SerialTerminalWidget


class SerialTab(QWidget):
    """Serial terminal tab for the device detail dialog.

    Each instance owns its own independent serial connection (separate from
    any transport used during pairing).
    """

    def __init__(self, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        layout = QVBoxLayout(self)
        layout.setContentsMargins(0, 0, 0, 0)
        self._terminal = SerialTerminalWidget()
        layout.addWidget(self._terminal)

    def closeEvent(self, event: object) -> None:  # type: ignore[override]
        self._terminal.closeEvent(event)
