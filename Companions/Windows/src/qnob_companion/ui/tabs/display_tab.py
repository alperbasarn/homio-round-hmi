"""Display & Audio configuration tab."""

from __future__ import annotations

import asyncio

from PySide6.QtWidgets import (
    QComboBox,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QSlider,
    QWidget,
)
from PySide6.QtCore import Qt

from qnob_companion.transport.client import DeviceClient
from qnob_companion.ui.tabs.base_tab import BaseTab

_SCREENS = [
    ("Info",         "info"),
    ("Device info",  "deviceInfo"),
    ("Timer",        "timer"),
    ("Light control","light"),
    ("Sound control","sound"),
    ("Temperature",  "temperature"),
    ("PC control",   "pc"),
]


class DisplayTab(BaseTab):
    def __init__(self, client: DeviceClient, parent: QWidget | None = None) -> None:
        self._brightness_slider = QSlider(Qt.Orientation.Horizontal)
        self._brightness_slider.setRange(0, 100)
        self._brightness_slider.setValue(100)
        self._brightness_val = QLabel("100 %")
        self._brightness_btn = QPushButton("Set Brightness")

        self._screen_combo = QComboBox()
        for label, _ in _SCREENS:
            self._screen_combo.addItem(label)
        self._screen_btn = QPushButton("Switch Screen")

        super().__init__(client, parent)
        self._brightness_slider.valueChanged.connect(
            lambda v: self._brightness_val.setText(f"{v} %")
        )
        self._brightness_btn.clicked.connect(
            lambda: asyncio.ensure_future(self._on_brightness())
        )
        self._screen_btn.clicked.connect(
            lambda: asyncio.ensure_future(self._on_screen())
        )

    def _build_form(self) -> None:
        f = self._form
        br_row = QHBoxLayout()
        br_row.addWidget(self._brightness_slider, 1)
        br_row.addWidget(self._brightness_val)
        f.addRow("Brightness", br_row)
        f.addRow("", self._brightness_btn)
        f.addRow("", QLabel(""))
        f.addRow("Switch to screen", self._screen_combo)
        f.addRow("", self._screen_btn)

    async def _on_brightness(self) -> None:
        pct = self._brightness_slider.value()
        await self._run(f"setBrightness:{pct}", success_msg=f"Brightness set to {pct} %")

    async def _on_screen(self) -> None:
        idx = self._screen_combo.currentIndex()
        _, cmd_name = _SCREENS[idx]
        await self._run(f"screen:{cmd_name}", success_msg=f"Switched to {cmd_name}")
