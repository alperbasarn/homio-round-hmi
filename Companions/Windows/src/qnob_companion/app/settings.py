"""User settings — persisted as JSON in the per-user config dir."""

from __future__ import annotations

import json
import logging
from pathlib import Path

from platformdirs import user_config_dir
from pydantic import BaseModel, Field

log = logging.getLogger(__name__)

APP_NAME = "Qnob"


class PairedDevice(BaseModel):
    """Persistent record of a device the user has paired with.

    The pairing token itself is NOT stored here — it lives in the OS
    credential store (see ``qnob_companion.pairing``).
    """

    mac: str
    name: str
    last_ip: str | None = None
    last_seen_iso: str | None = None
    transport_preference: str = "tcp"  # "tcp" | "ble"


class Settings(BaseModel):
    """Top-level settings document."""

    schema_version: int = 1
    paired_devices: list[PairedDevice] = Field(default_factory=list)
    autostart_minimized: bool = False
    log_level: str = "INFO"


def settings_path() -> Path:
    return Path(user_config_dir(APP_NAME)) / "settings.json"


def load() -> Settings:
    """Load settings from disk, returning defaults if absent or corrupt."""
    path = settings_path()
    if not path.exists():
        log.info("No settings file at %s; using defaults", path)
        return Settings()
    try:
        raw = path.read_text(encoding="utf-8")
        return Settings.model_validate_json(raw)
    except (json.JSONDecodeError, ValueError) as exc:
        log.warning("Settings at %s unreadable (%s); using defaults", path, exc)
        return Settings()


def save(settings: Settings) -> None:
    """Persist settings atomically (write-temp-then-rename)."""
    path = settings_path()
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(".json.tmp")
    tmp.write_text(settings.model_dump_json(indent=2), encoding="utf-8")
    tmp.replace(path)
