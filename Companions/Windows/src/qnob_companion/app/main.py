"""Entry point — boots logging, settings, and the PySide6 + asyncio main window."""

from __future__ import annotations

import asyncio
import logging
import sys

import qasync
from PySide6.QtWidgets import QApplication

from qnob_companion.app import logging_config, settings as settings_module
from qnob_companion.ui.main_window import MainWindow

log = logging.getLogger(__name__)


def main() -> int:
    settings = settings_module.load()
    log_path = logging_config.configure(settings.log_level)
    log.info("Qnob companion starting; logs at %s", log_path)

    qt_app = QApplication(sys.argv)
    qt_app.setApplicationName("Qnob Companion")

    loop = qasync.QEventLoop(qt_app)
    asyncio.set_event_loop(loop)

    window = MainWindow(settings)
    window.show()

    with loop:
        loop.run_forever()

    settings_module.save(window.current_settings())
    log.info("Qnob companion exiting")
    return 0


if __name__ == "__main__":
    sys.exit(main())
