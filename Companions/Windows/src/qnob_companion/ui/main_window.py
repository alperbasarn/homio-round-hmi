"""Main application window — sidebar nav + page stack."""

from __future__ import annotations

from PySide6.QtCore import Qt
from PySide6.QtGui import QDesktopServices
from PySide6.QtWidgets import (
    QFrame,
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
from qnob_companion.app.updater import UpdateInfo
from qnob_companion.discovery.service import DiscoveryService
from qnob_companion.pairing.secret_store import KeyringSecretStore, SecretStore
from qnob_companion.ui.devices_page import DevicesPage


class _UpdateBanner(QFrame):
    """Dismissable banner shown at the top of the window when an update is available."""

    def __init__(self, info: UpdateInfo, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._url = info.release_url
        self.setFrameShape(QFrame.Shape.StyledPanel)
        self.setStyleSheet(
            "QFrame { background: #2980b9; padding: 0; }"
            "QLabel { color: white; font-size: 12px; }"
            "QPushButton { color: white; background: transparent; border: 1px solid rgba(255,255,255,0.6);"
            "  border-radius: 3px; padding: 2px 10px; font-size: 12px; }"
            "QPushButton:hover { background: rgba(255,255,255,0.15); }"
        )

        row = QHBoxLayout(self)
        row.setContentsMargins(12, 6, 12, 6)

        lbl = QLabel(
            f"Qnob Companion {info.version} is available. "
            "Download and install from the GitHub release."
        )
        row.addWidget(lbl, 1)

        view_btn = QPushButton("View Release")
        view_btn.clicked.connect(self._open_release)
        row.addWidget(view_btn)

        dismiss_btn = QPushButton("Later")
        dismiss_btn.clicked.connect(self.hide)
        row.addWidget(dismiss_btn)

    def _open_release(self) -> None:
        from PySide6.QtCore import QUrl
        QDesktopServices.openUrl(QUrl(self._url))


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
        root = QVBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # Update banner — hidden until show_update() is called.
        self._update_banner: _UpdateBanner | None = None
        self._banner_slot = QWidget()  # placeholder; replaced by banner when needed
        self._banner_slot.hide()
        root.addWidget(self._banner_slot)

        # Main body: sidebar + page stack.
        body = QWidget()
        layout = QHBoxLayout(body)
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
        root.addWidget(body, 1)
        self.setCentralWidget(central)

    # ----- lifecycle -----

    def start(self) -> None:
        """Start background tasks (call after asyncio event loop is running)."""
        self.devices_page.start()

    def stop(self) -> None:
        """Stop background tasks (call before event loop shuts down)."""
        self.devices_page.stop()

    # ----- update notification -----

    def show_update(self, info: UpdateInfo) -> None:
        """Display the update banner. Safe to call from any asyncio coroutine."""
        if self._update_banner is not None:
            return  # already shown

        central = self.centralWidget()
        root_layout = central.layout()

        banner = _UpdateBanner(info, central)
        self._update_banner = banner
        root_layout.insertWidget(0, banner)
        self._banner_slot.hide()

    # ----- settings persistence -----

    def current_settings(self) -> Settings:
        return self._settings
