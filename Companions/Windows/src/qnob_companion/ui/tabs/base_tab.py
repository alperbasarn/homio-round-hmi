"""Shared base class for every config tab in the device detail dialog."""

from __future__ import annotations

import asyncio
import logging
from typing import Any

from PySide6.QtCore import QTimer, Qt
from PySide6.QtWidgets import (
    QFormLayout,
    QHBoxLayout,
    QLabel,
    QVBoxLayout,
    QWidget,
)

from qnob_companion.protocol import Response
from qnob_companion.transport.client import DeviceClient

log = logging.getLogger(__name__)


class BaseTab(QWidget):
    """Common tab shell: form area + status bar + async command runner.

    Subclasses override :meth:`_build_form` to add their controls and
    call :meth:`_run` to dispatch commands.

    Lazy loading: override :meth:`_load` and it will be called the first
    time the tab is made visible via :meth:`activate`.
    """

    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._client = client
        self._loaded = False
        self._busy = False

        root = QVBoxLayout(self)
        root.setContentsMargins(16, 12, 16, 12)
        root.setSpacing(10)

        # Form area — subclasses populate this
        self._form = QFormLayout()
        self._form.setLabelAlignment(Qt.AlignmentFlag.AlignRight)
        self._form.setFieldGrowthPolicy(QFormLayout.FieldGrowthPolicy.ExpandingFieldsGrow)
        root.addLayout(self._form)
        root.addStretch()

        # Status bar at the bottom
        self._status_lbl = QLabel("")
        self._status_lbl.setWordWrap(True)
        root.addWidget(self._status_lbl)

        self._build_form()

    # ----- subclass hooks -----

    def _build_form(self) -> None:
        """Populate self._form. Called once from __init__."""

    async def _load(self) -> None:
        """Fetch device state to pre-populate fields. Called once on first activate."""

    # ----- lifecycle -----

    def activate(self) -> None:
        """Called by DeviceDetailDialog.currentChanged when this tab is shown."""
        if not self._loaded:
            self._loaded = True
            asyncio.ensure_future(self._load())

    # ----- async command helpers -----

    async def _run(
        self,
        cmd: str,
        params: dict[str, Any] | None = None,
        *,
        success_msg: str = "Saved",
    ) -> Response | None:
        """Send a command, show a status toast, return the Response."""
        if self._busy:
            return None
        self._set_busy(True)
        try:
            resp = await self._client.send_command(cmd, params)
            if resp.is_ok:
                self._toast(success_msg, error=False)
            else:
                self._toast(f"Error: {resp.error or 'unknown'}", error=True)
            return resp
        except Exception as exc:
            self._toast(f"Error: {exc}", error=True)
            log.warning("Command %s failed: %s", cmd, exc)
            return None
        finally:
            self._set_busy(False)

    async def _fetch_status(self) -> dict[str, Any] | None:
        """Fetch device status silently (no toast) for field pre-population."""
        try:
            resp = await self._client.send_command("status")
            return resp.data if resp.is_ok else None
        except Exception:
            return None

    # ----- UI helpers -----

    def _set_busy(self, busy: bool) -> None:
        self._busy = busy
        # Disable every child widget that isn't the status label.
        for child in self.findChildren(QWidget):
            if child is not self._status_lbl:
                child.setEnabled(not busy)

    def _toast(self, message: str, *, error: bool = False) -> None:
        color = "#c0392b" if error else "#27ae60"
        self._status_lbl.setStyleSheet(f"color: {color};")
        self._status_lbl.setText(message)
        QTimer.singleShot(4000, lambda: self._status_lbl.setText(""))
