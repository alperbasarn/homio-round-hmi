"""Settings load/save round-trip tests."""

from __future__ import annotations

from pathlib import Path
from unittest.mock import patch

from qnob_companion.app import settings as settings_module
from qnob_companion.app.settings import PairedDevice, Settings


def test_load_returns_defaults_when_missing(tmp_path: Path) -> None:
    target = tmp_path / "settings.json"
    with patch.object(settings_module, "settings_path", return_value=target):
        loaded = settings_module.load()
    assert loaded.paired_devices == []
    assert loaded.schema_version == 1


def test_save_then_load_round_trip(tmp_path: Path) -> None:
    target = tmp_path / "settings.json"
    original = Settings(
        paired_devices=[PairedDevice(mac="aabbccddeeff", name="Bench Qnob", last_ip="192.168.1.50")],
        autostart_minimized=True,
    )
    with patch.object(settings_module, "settings_path", return_value=target):
        settings_module.save(original)
        loaded = settings_module.load()
    assert loaded.paired_devices[0].mac == "aabbccddeeff"
    assert loaded.paired_devices[0].name == "Bench Qnob"
    assert loaded.autostart_minimized is True


def test_load_returns_defaults_on_corrupt_file(tmp_path: Path) -> None:
    target = tmp_path / "settings.json"
    target.write_text("not json", encoding="utf-8")
    with patch.object(settings_module, "settings_path", return_value=target):
        loaded = settings_module.load()
    assert loaded.paired_devices == []


def test_save_atomic_via_tmp_rename(tmp_path: Path) -> None:
    target = tmp_path / "settings.json"
    with patch.object(settings_module, "settings_path", return_value=target):
        settings_module.save(Settings())
    assert target.exists()
    assert not target.with_suffix(".json.tmp").exists()
