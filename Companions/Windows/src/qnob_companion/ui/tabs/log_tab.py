"""Log tab — real-time device log stream via push notifications."""

from __future__ import annotations

import asyncio
import logging
from collections import deque
from pathlib import Path
from typing import Any

from PySide6.QtCore import QTimer
from PySide6.QtGui import QFont, QHideEvent, QShowEvent
from PySide6.QtWidgets import (
    QCheckBox,
    QComboBox,
    QFileDialog,
    QHBoxLayout,
    QLabel,
    QLineEdit,
    QPlainTextEdit,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from qnob_companion.transport.client import DeviceClient

log = logging.getLogger(__name__)

_MAX_ENTRIES = 10_000
_LEVELS = ("ALL", "DEBUG", "INFO", "WARN", "ERROR")

# ESP-IDF single-char level codes → canonical names
_LEVEL_CHARS: dict[str, str] = {
    "D": "DEBUG", "d": "DEBUG",
    "I": "INFO",  "i": "INFO",
    "W": "WARN",  "w": "WARN",
    "E": "ERROR", "e": "ERROR",
}

# Severity rank for min-level filtering
_LEVEL_RANK: dict[str, int] = {"DEBUG": 0, "INFO": 1, "WARN": 2, "ERROR": 3}


def _matches_filter(entry: dict[str, str], min_level: str, text: str) -> bool:
    """Return True if *entry* passes the level and text filters.

    Pure function so it can be unit-tested without Qt.
    """
    if min_level != "ALL":
        entry_rank = _LEVEL_RANK.get(entry.get("level", ""), 0)
        min_rank = _LEVEL_RANK.get(min_level, 0)
        if entry_rank < min_rank:
            return False
    if text:
        haystack = f"{entry.get('tag', '')} {entry.get('message', '')}".lower()
        if text not in haystack:
            return False
    return True


def _fmt_entry(entry: dict[str, str]) -> str:
    level = entry.get("level", "?")
    tag = entry.get("tag", "")
    msg = entry.get("message", "")
    return f"[{level:5s}] {tag}: {msg}" if tag else f"[{level:5s}] {msg}"


class LogTab(QWidget):
    """Streams device log notifications while visible.

    Log entries arrive as Notifications with event="log" and
    data={level, tag, message} (ESP-IDF log format).
    Up to 10 000 entries are kept in memory; older entries are evicted.
    """

    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        super().__init__(parent)
        self._client = client
        self._entries: deque[dict[str, str]] = deque(maxlen=_MAX_ENTRIES)
        self._drain_task: asyncio.Task[None] | None = None

        self._filter_timer = QTimer()
        self._filter_timer.setSingleShot(True)
        self._filter_timer.setInterval(250)
        self._filter_timer.timeout.connect(self._apply_filter)

        self._build_ui()

    # ----- UI construction -----

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(8, 8, 8, 8)
        root.setSpacing(6)

        # ── Filter bar ────────────────────────────────────────────────────
        filter_row = QHBoxLayout()
        filter_row.addWidget(QLabel("Level:"))

        self._level_combo = QComboBox()
        self._level_combo.addItems(_LEVELS)
        self._level_combo.currentIndexChanged.connect(self._on_filter_changed)
        filter_row.addWidget(self._level_combo)

        filter_row.addSpacing(8)
        filter_row.addWidget(QLabel("Filter:"))

        self._filter_edit = QLineEdit()
        self._filter_edit.setPlaceholderText("tag or message substring…")
        self._filter_edit.textChanged.connect(self._on_filter_changed)
        filter_row.addWidget(self._filter_edit, 1)

        self._clear_btn = QPushButton("Clear")
        self._clear_btn.clicked.connect(self._on_clear)
        filter_row.addWidget(self._clear_btn)
        root.addLayout(filter_row)

        # ── Log view ──────────────────────────────────────────────────────
        self._log_view = QPlainTextEdit()
        self._log_view.setReadOnly(True)
        mono = QFont("Consolas")
        mono.setStyleHint(QFont.StyleHint.Monospace)
        mono.setPointSize(9)
        self._log_view.setFont(mono)
        self._log_view.setMaximumBlockCount(0)  # unlimited; we manage size via deque
        root.addWidget(self._log_view, 1)

        # ── Bottom bar ────────────────────────────────────────────────────
        bottom_row = QHBoxLayout()

        self._autoscroll_chk = QCheckBox("Auto-scroll")
        self._autoscroll_chk.setChecked(True)
        bottom_row.addWidget(self._autoscroll_chk)

        bottom_row.addStretch()

        self._count_lbl = QLabel("")
        self._count_lbl.setStyleSheet("color: #888; font-size: 11px;")
        bottom_row.addWidget(self._count_lbl)
        bottom_row.addSpacing(8)

        save_btn = QPushButton("Save to file…")
        save_btn.clicked.connect(self._on_save)
        bottom_row.addWidget(save_btn)
        root.addLayout(bottom_row)

    # ----- lifecycle -----

    def activate(self) -> None:
        """Called by DeviceDetailDialog each time this tab is selected."""
        self._ensure_drain_running()

    def showEvent(self, event: QShowEvent) -> None:
        super().showEvent(event)
        self._ensure_drain_running()

    def hideEvent(self, event: QHideEvent) -> None:
        super().hideEvent(event)
        if self._drain_task is not None and not self._drain_task.done():
            self._drain_task.cancel()
        self._drain_task = None

    # ----- drain task -----

    def _ensure_drain_running(self) -> None:
        if self._drain_task is None or self._drain_task.done():
            self._drain_task = asyncio.ensure_future(self._drain_loop())

    async def _drain_loop(self) -> None:
        try:
            while True:
                transport = self._client.active_transport
                if transport is None:
                    await asyncio.sleep(0.5)
                    continue
                try:
                    notification = await asyncio.wait_for(
                        transport.notifications.get(), timeout=0.5
                    )
                    if notification.event == "log":
                        self._ingest(notification.data or {})
                except asyncio.TimeoutError:
                    # Loop back — lets us detect transport changes and stop events.
                    continue
        except asyncio.CancelledError:
            pass

    # ----- entry handling -----

    def _ingest(self, data: dict[str, Any]) -> None:
        raw_level = str(data.get("level", "I"))
        level = _LEVEL_CHARS.get(raw_level, raw_level.upper())
        entry: dict[str, str] = {
            "level": level,
            "tag": str(data.get("tag", "")),
            "message": str(data.get("message", "")),
        }
        self._entries.append(entry)
        self._count_lbl.setText(f"{len(self._entries):,} entries")
        if _matches_filter(
            entry,
            self._level_combo.currentText(),
            self._filter_edit.text().strip().lower(),
        ):
            self._log_view.appendPlainText(_fmt_entry(entry))
            self._scroll_to_bottom()

    # ----- filtering -----

    def _on_filter_changed(self) -> None:
        self._filter_timer.start()

    def _apply_filter(self) -> None:
        min_level = self._level_combo.currentText()
        text = self._filter_edit.text().strip().lower()
        lines = [
            _fmt_entry(e)
            for e in self._entries
            if _matches_filter(e, min_level, text)
        ]
        self._log_view.setPlainText("\n".join(lines))
        self._scroll_to_bottom()

    def _scroll_to_bottom(self) -> None:
        if self._autoscroll_chk.isChecked():
            vsb = self._log_view.verticalScrollBar()
            vsb.setValue(vsb.maximum())

    # ----- slots -----

    def _on_clear(self) -> None:
        self._entries.clear()
        self._log_view.clear()
        self._count_lbl.setText("")

    def _on_save(self) -> None:
        path, _ = QFileDialog.getSaveFileName(
            self,
            "Save device log",
            "device_log.txt",
            "Text files (*.txt);;All files (*)",
        )
        if not path:
            return
        lines = [_fmt_entry(e) for e in self._entries]
        Path(path).write_text("\n".join(lines), encoding="utf-8")
