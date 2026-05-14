"""Main application window — sidebar nav + page stack."""

from __future__ import annotations

from PySide6.QtCore import Qt, Slot
from PySide6.QtWidgets import (
    QHBoxLayout,
    QLabel,
    QListWidget,
    QListWidgetItem,
    QMainWindow,
    QPushButton,
    QStackedWidget,
    QVBoxLayout,
    QWidget,
)

from qnob_companion.app.settings import Settings
from qnob_companion.discovery.descriptor import DeviceDescriptor
from qnob_companion.discovery.service import DiscoveryService
from qnob_companion.pairing.secret_store import KeyringSecretStore, SecretStore


class _PlaceholderPage(QWidget):
    """Stub page shown while real pages land in later tickets."""

    def __init__(self, title: str, message: str) -> None:
        super().__init__()
        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(QLabel(f"<h2>{title}</h2>", alignment=Qt.AlignmentFlag.AlignCenter))
        layout.addWidget(QLabel(message, alignment=Qt.AlignmentFlag.AlignCenter))


class _DevicesPage(QWidget):
    """Device list placeholder with a 'Pair New Device' button.

    The full device dashboard lands in #66 C1.
    """

    def __init__(
        self,
        discovery: DiscoveryService,
        secret_store: SecretStore,
    ) -> None:
        super().__init__()
        self._discovery = discovery
        self._secret_store = secret_store

        layout = QVBoxLayout(self)
        layout.setAlignment(Qt.AlignmentFlag.AlignCenter)
        layout.addWidget(
            QLabel(
                "<h2>Devices</h2>",
                alignment=Qt.AlignmentFlag.AlignCenter,
            )
        )
        layout.addWidget(
            QLabel(
                "Discover and pair Qnob devices on your network or in BLE range.\n"
                "(Full dashboard lands in #66 C1.)",
                alignment=Qt.AlignmentFlag.AlignCenter,
            )
        )
        layout.addSpacing(16)

        pair_btn = QPushButton("Pair New Device…")
        pair_btn.clicked.connect(self._open_pairing_wizard)
        layout.addWidget(pair_btn, alignment=Qt.AlignmentFlag.AlignCenter)

    @Slot()
    def _open_pairing_wizard(self) -> None:
        # Import here to avoid a circular dependency at module load time.
        from qnob_companion.ui.pairing_wizard import PairingWizard

        wizard = PairingWizard(
            self._discovery,
            self._secret_store,
            parent=self,
        )
        wizard.paired.connect(self._on_paired)
        wizard.exec()

    @Slot(object)
    def _on_paired(self, device: DeviceDescriptor) -> None:
        name = device.name or device.mac
        # Notify parent window (or log) — full UI feedback lands in #66.
        import logging
        logging.getLogger(__name__).info("Paired device: %s", name)


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

        # Devices page — has "Pair New Device" button.
        QListWidgetItem("Devices", self._sidebar)
        self._stack.addWidget(
            _DevicesPage(self._discovery, self._secret_store)
        )

        # Settings page — placeholder pending #66.
        QListWidgetItem("Settings", self._sidebar)
        self._stack.addWidget(
            _PlaceholderPage(
                "Settings",
                "Companion app preferences.\n(Coming with #66+ once dashboard exists.)",
            )
        )

        self._sidebar.currentRowChanged.connect(self._stack.setCurrentIndex)
        self._sidebar.setCurrentRow(0)

        layout.addWidget(self._sidebar)
        layout.addWidget(self._stack, 1)
        self.setCentralWidget(central)

    def current_settings(self) -> Settings:
        """Return the (possibly modified) settings document for persistence on shutdown."""
        return self._settings
