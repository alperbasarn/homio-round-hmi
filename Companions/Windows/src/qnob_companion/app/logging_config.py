"""Logging configuration — console + rotating file."""

from __future__ import annotations

import logging
import logging.handlers
from pathlib import Path

from platformdirs import user_log_dir

APP_NAME = "Qnob"


def configure(level: str = "INFO") -> Path:
    """Configure root logger; return path to the log file."""
    log_dir = Path(user_log_dir(APP_NAME))
    log_dir.mkdir(parents=True, exist_ok=True)
    log_path = log_dir / "companion.log"

    fmt = "%(asctime)s [%(levelname)s] %(name)s: %(message)s"
    formatter = logging.Formatter(fmt, datefmt="%Y-%m-%d %H:%M:%S")

    root = logging.getLogger()
    root.setLevel(level.upper())

    # Avoid duplicate handlers if configure() is called more than once.
    for h in list(root.handlers):
        root.removeHandler(h)

    console = logging.StreamHandler()
    console.setFormatter(formatter)
    root.addHandler(console)

    rotating = logging.handlers.RotatingFileHandler(
        log_path, maxBytes=5 * 1024 * 1024, backupCount=3, encoding="utf-8"
    )
    rotating.setFormatter(formatter)
    root.addHandler(rotating)

    return log_path
