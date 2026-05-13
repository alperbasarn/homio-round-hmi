"""Main application window — sidebar nav + page stack."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from qnob_companion.app.settings import Settings


class _PlaceholderPage(QWidget):
    """Stub page shown while real pages land in later tickets."""

    def __init__(self, title: str, message: str) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(QLabel(f"<h2>{title}</h2>", alignment=Qt.AlignmentFlag.AlignCenter))
        layout.addWidget(QLabel(message, alignment=Qt.AlignmentFlag.AlignCenter))


class MainWindow(QMainWindow):
    """Top-level shell: left sidebar with nav items, right pane with page stack."""

    NAV_ITEMS: tuple[tuple[str, str], ...] = (
        ("Devices", "Discover and pair Qnob devices on your network or in BLE range.\n(Implementation lands in #66 C1 — dashboard.)"),
        ("Settings", "Companion app preferences.\n(Coming with #66+ once dashboard exists.)"),
    )

    def __init__(self, settings: Settings) -> None:
        super().__init__()
        self._settings = settings

        self.setWindowTitle("Qnob Companion")
        self.resize(1000, 650)
        self.setMinimumSize(720, 480)

        central = QWidget()
        layout = QHBoxLayout(central)
        layout.setContentsMargins(0, 0, 0, 0)
        layout.setSpacing(0)

        self._sidebar = QListWidget()
        self._sidebar.setFixedWidth(180)
        self._sidebar.setFrameShape(QListWidget.Shape.NoFrame)

        self._stack = QStackedWidget()

        for label, description in self.NAV_ITEMS:
            QListWidgetItem(label, self._sidebar)
            self._stack.addWidget(_PlaceholderPage(label, description))

        self._sidebar.currentRowChanged.connect(self._stack.setCurrentIndex)
        self._sidebar.setCurrentRow(0)

        layout.addWidget(self._sidebar)
        layout.addWidget(self._stack, 1)
        self.setCentralWidget(central)

    def current_settings(self) -> Settings:
        """Return the (possibly modified) settings document for persistence on shutdown."""
        return self._settings
