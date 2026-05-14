"""Main application window — sidebar nav + page stack."""

from __future__ import annotations

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
from PySide6.QtCore import Qt

from qnob_companion.app.settings import Settings
from qnob_companion.discovery.service import DiscoveryService
from qnob_companion.pairing.secret_store import KeyringSecretStore, SecretStore
from qnob_companion.ui.devices_page import DevicesPage


class _PlaceholderPage(QWidget):
    """Stub page for sidebar items without a real implementation yet."""

    def __init__(self, title: str, message: str) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(QLabel(f"<h2>{title}</h2>", alignment=Qt.AlignmentFlag.AlignCenter))
        layout.addWidget(QLabel(message, alignment=Qt.AlignmentFlag.AlignCenter))


class MainWindow(QMainWindow):
    """Top-level shell: left sidebar with nav items, right pane with page stack."""

    def __init__(
        self,
        settings: Settings,
        *,
        discovery: DiscoveryService | None = None,
        secret_store: SecretStore | None = None,
    ) -> None:
        super().__init__()
        self._settings = settings
        self._discovery = discovery or DiscoveryService()
        self._secret_store: SecretStore = secret_store or KeyringSecretStore()

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

        # Devices page — live dashboard (C1).
        self.devices_page = DevicesPage(self._discovery, self._secret_store)
        QListWidgetItem("Devices", self._sidebar)
        self._stack.addWidget(self.devices_page)

        # Settings page — placeholder pending later C-tickets.
        QListWidgetItem("Settings", self._sidebar)
        self._stack.addWidget(
            _PlaceholderPage(
                "Settings",
                "Companion app preferences.\n(Coming in a later ticket.)",
            )
        )

        self._sidebar.currentRowChanged.connect(self._stack.setCurrentIndex)
        self._sidebar.setCurrentRow(0)

        layout.addWidget(self._sidebar)
        layout.addWidget(self._stack, 1)
        self.setCentralWidget(central)

    # ----- lifecycle -----

    def start(self) -> None:
        """Start background tasks (call after asyncio event loop is running)."""
        self.devices_page.start()

    def stop(self) -> None:
        """Stop background tasks (call before event loop shuts down)."""
        self.devices_page.stop()

    # ----- settings persistence -----

    def current_settings(self) -> Settings:
        return self._settings
