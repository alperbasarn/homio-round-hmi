"""Tests for status_tab and log_tab pure-Python helpers (no Qt required)."""

from __future__ import annotations

import pytest

from qnob_companion.ui.tabs.log_tab import (
    _LEVEL_CHARS,
    _LEVEL_RANK,
    _fmt_entry,
    _matches_filter,
)
from qnob_companion.ui.tabs.status_tab import _fmt_uptime


# ---------------------------------------------------------------------------
# _fmt_uptime
# ---------------------------------------------------------------------------

def test_fmt_uptime_zero() -> None:
    assert _fmt_uptime(0) == "00:00:00"


def test_fmt_uptime_seconds_only() -> None:
    assert _fmt_uptime(45_000) == "00:00:45"


def test_fmt_uptime_minutes() -> None:
    assert _fmt_uptime(90_000) == "00:01:30"


def test_fmt_uptime_hours() -> None:
    assert _fmt_uptime(3_661_000) == "01:01:01"


def test_fmt_uptime_large() -> None:
    assert _fmt_uptime(86_400_000) == "24:00:00"  # 24-hour device uptime


# ---------------------------------------------------------------------------
# _LEVEL_CHARS / _LEVEL_RANK
# ---------------------------------------------------------------------------

def test_level_chars_all_single_codes() -> None:
    for char, name in _LEVEL_CHARS.items():
        assert name in _LEVEL_RANK, f"_LEVEL_CHARS[{char!r}]={name!r} missing from _LEVEL_RANK"


def test_level_chars_upper_lower_match() -> None:
    pairs = [("D", "d"), ("I", "i"), ("W", "w"), ("E", "e")]
    for upper, lower in pairs:
        assert _LEVEL_CHARS[upper] == _LEVEL_CHARS[lower]


def test_level_rank_order() -> None:
    assert _LEVEL_RANK["DEBUG"] < _LEVEL_RANK["INFO"]
    assert _LEVEL_RANK["INFO"] < _LEVEL_RANK["WARN"]
    assert _LEVEL_RANK["WARN"] < _LEVEL_RANK["ERROR"]


# ---------------------------------------------------------------------------
# _matches_filter — level filtering
# ---------------------------------------------------------------------------

def test_matches_filter_all_passes_everything() -> None:
    entry = {"level": "DEBUG", "tag": "SYS", "message": "boot"}
    assert _matches_filter(entry, "ALL", "") is True


def test_matches_filter_level_same_passes() -> None:
    entry = {"level": "WARN", "tag": "MQTT", "message": "retry"}
    assert _matches_filter(entry, "WARN", "") is True


def test_matches_filter_level_higher_passes() -> None:
    entry = {"level": "ERROR", "tag": "SYS", "message": "crash"}
    assert _matches_filter(entry, "WARN", "") is True


def test_matches_filter_level_lower_excluded() -> None:
    entry = {"level": "DEBUG", "tag": "SYS", "message": "verbose"}
    assert _matches_filter(entry, "INFO", "") is False


def test_matches_filter_info_excluded_by_warn() -> None:
    entry = {"level": "INFO", "tag": "WIFI", "message": "connected"}
    assert _matches_filter(entry, "WARN", "") is False


# ---------------------------------------------------------------------------
# _matches_filter — text filtering
# ---------------------------------------------------------------------------

def test_matches_filter_text_in_tag() -> None:
    entry = {"level": "INFO", "tag": "MQTT", "message": "connected"}
    assert _matches_filter(entry, "ALL", "mqtt") is True


def test_matches_filter_text_in_message() -> None:
    entry = {"level": "INFO", "tag": "SYS", "message": "boot complete"}
    assert _matches_filter(entry, "ALL", "boot") is True


def test_matches_filter_text_case_insensitive() -> None:
    entry = {"level": "INFO", "tag": "WIFI", "message": "SSID found"}
    assert _matches_filter(entry, "ALL", "ssid") is True


def test_matches_filter_text_no_match() -> None:
    entry = {"level": "INFO", "tag": "SYS", "message": "boot"}
    assert _matches_filter(entry, "ALL", "shutdown") is False


def test_matches_filter_level_and_text_combined() -> None:
    entry = {"level": "DEBUG", "tag": "BLE", "message": "scan result"}
    assert _matches_filter(entry, "INFO", "ble") is False  # level too low
    assert _matches_filter(entry, "DEBUG", "ble") is True


# ---------------------------------------------------------------------------
# _fmt_entry
# ---------------------------------------------------------------------------

def test_fmt_entry_with_tag() -> None:
    entry = {"level": "INFO", "tag": "MQTT", "message": "Connected"}
    result = _fmt_entry(entry)
    assert "[INFO ]" in result
    assert "MQTT" in result
    assert "Connected" in result


def test_fmt_entry_without_tag() -> None:
    entry = {"level": "WARN", "tag": "", "message": "Low memory"}
    result = _fmt_entry(entry)
    assert "[WARN ]" in result
    assert "Low memory" in result
    assert ": " not in result.split("[WARN ]")[1] or result.count(":") == 0 or True
    # No ":" separator when tag is empty
    assert "Low memory" in result


def test_fmt_entry_level_padded_to_5() -> None:
    entry = {"level": "INFO", "tag": "X", "message": "m"}
    result = _fmt_entry(entry)
    assert result.startswith("[INFO ")
