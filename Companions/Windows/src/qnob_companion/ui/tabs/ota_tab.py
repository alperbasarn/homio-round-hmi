"""OTA firmware updater tab — TCP-only binary push."""

from __future__ import annotations

import asyncio
from pathlib import Path

from PySide6.QtCore import Qt, Slot
from PySide6.QtWidgets import (
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QMessageBox,
    QProgressBar,
    QPushButton,
    QWidget,
)

from qnob_companion.ota.uploader import MAX_FIRMWARE_BYTES, OtaError, OtaUploader
from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.base_tab import BaseTab


class OtaTab(BaseTab):
    """Chunked firmware upload over TCP.

    Disabled automatically when only a BLE transport is connected — OTA
    requires TCP (per docs/protocol.md §4.10).
    """

    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        self._firmware_path: Path | None = None
        self._uploader: OtaUploader | None = None

        # Pre-flight info row
        self._current_ver_lbl = QLabel("—")
        self._file_lbl = QLabel("No file selected")
        self._file_lbl.setWordWrap(True)

        # Buttons
        self._pick_btn = QPushButton("Browse…")
        self._start_btn = QPushButton("Flash Firmware")
        self._start_btn.setEnabled(False)
        self._cancel_btn = QPushButton("Cancel")
        self._cancel_btn.setEnabled(False)
        self._cancel_btn.setStyleSheet("color: #c0392b;")

        # Progress
        self._progress = QProgressBar()
        self._progress.setRange(0, 100)
        self._progress.setValue(0)
        self._progress.setVisible(False)
        self._progress_lbl = QLabel("")
        self._progress_lbl.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self._progress_lbl.setStyleSheet("font-size: 11px; color: #555;")

        super().__init__(client, parent)

        self._pick_btn.clicked.connect(self._on_pick)
        self._start_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_start()))
        self._cancel_btn.clicked.connect(lambda: asyncio.ensure_future(self._on_cancel()))

        # Show warning if no TCP.
        self._check_transport()

    # ----- form builder -----

    def _build_form(self) -> None:
        f = self._form
        f.addRow("Current firmware", self._current_ver_lbl)
        f.addRow("", QLabel(""))

        pick_row = QHBoxLayout()
        pick_row.addWidget(self._file_lbl, 1)
        pick_row.addWidget(self._pick_btn)
        f.addRow("Firmware file (.bin)", pick_row)
        f.addRow("", QLabel(f"Maximum file size: {MAX_FIRMWARE_BYTES // (1024*1024)} MiB"))
        f.addRow("", QLabel(""))

        f.addRow(self._progress)
        f.addRow("", self._progress_lbl)

        btn_row = QHBoxLayout()
        btn_row.addWidget(self._start_btn)
        btn_row.addWidget(self._cancel_btn)
        f.addRow("", btn_row)

    # ----- lifecycle -----

    async def _load(self) -> None:
        try:
            resp = await self._client.send_command("otaInfo")
            if resp.is_ok and resp.data:
                ver = resp.data.get("version") or resp.data.get("result") or "unknown"
                self._current_ver_lbl.setText(str(ver))
        except Exception:
            self._current_ver_lbl.setText("(could not read)")
        self._check_transport()

    # ----- slots -----

    @Slot()
    def _on_pick(self) -> None:
        path, _ = QFileDialog.getOpenFileName(
            self,
            "Select firmware binary",
            "",
            "Firmware files (*.bin);;All files (*)",
        )
        if not path:
            return

        firmware_path = Path(path)
        size = firmware_path.stat().st_size
        if size > MAX_FIRMWARE_BYTES:
            QMessageBox.warning(
                self,
                "File too large",
                f"The selected file is {size / 1024 / 1024:.1f} MiB.\n"
                f"Maximum allowed: {MAX_FIRMWARE_BYTES // (1024*1024)} MiB.",
            )
            return

        self._firmware_path = firmware_path
        self._file_lbl.setText(f"{firmware_path.name}  ({size / 1024:.0f} KB)")
        self._start_btn.setEnabled(True)

    async def _on_start(self) -> None:
        if self._firmware_path is None:
            return

        # Confirm
        box = QMessageBox(self)
        box.setWindowTitle("Flash Firmware")
        box.setText(
            f"Flash <b>{self._firmware_path.name}</b> to the device?\n\n"
            "The device will restart after a successful upload."
        )
        box.setStandardButtons(
            QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.Cancel
        )
        box.setDefaultButton(QMessageBox.StandardButton.Cancel)
        if box.exec() != QMessageBox.StandardButton.Yes:
            return

        self._uploader = OtaUploader(self._client, self._firmware_path)
        self._set_uploading(True)

        try:
            await self._uploader.start(on_progress=self._on_progress)
            self._toast("Upload complete — device is rebooting", error=False)
            self._progress.setValue(100)
        except OtaError as exc:
            self._toast(str(exc), error=True)
            self._progress.setValue(0)
        finally:
            self._set_uploading(False)
            self._uploader = None

    async def _on_cancel(self) -> None:
        if self._uploader is not None:
            await self._uploader.cancel()

    # ----- helpers -----

    def _on_progress(self, acked: int, total: int, rate: float, eta: float) -> None:
        pct = int(acked * 100 / total) if total else 0
        self._progress.setValue(pct)
        self._progress_lbl.setText(
            f"{acked / 1024:.0f} / {total / 1024:.0f} KB  ·  "
            f"{rate / 1024:.0f} KB/s  ·  ETA {eta:.0f} s"
        )

    def _set_uploading(self, uploading: bool) -> None:
        self._progress.setVisible(True)
        self._pick_btn.setEnabled(not uploading)
        self._start_btn.setEnabled(not uploading)
        self._cancel_btn.setEnabled(uploading)

    def _check_transport(self) -> None:
        tcp = self._client._tcp  # type: ignore[attr-defined]
        has_tcp = tcp is not None
        if not has_tcp:
            self._toast(
                "OTA requires WiFi — connect the device to your network first.",
                error=True,
            )
            self._start_btn.setEnabled(False)
