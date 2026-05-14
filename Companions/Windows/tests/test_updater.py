"""Unit tests for the auto-updater pure helpers — no network, no Qt."""

from __future__ import annotations

import pytest

from qnob_companion.app.updater import UpdateInfo, _is_newer, _parse_version


# ---------------------------------------------------------------------------
# _parse_version
# ---------------------------------------------------------------------------

def test_parse_simple() -> None:
    assert _parse_version("1.2.3") == (1, 2, 3)


def test_parse_with_v_prefix() -> None:
    assert _parse_version("v0.1.0") == (0, 1, 0)


def test_parse_truncates_to_three_parts() -> None:
    assert _parse_version("1.2.3.4") == (1, 2, 3)


def test_parse_strips_prerelease_label() -> None:
    assert _parse_version("1.0.0-alpha.1") == (1, 0, 0)


def test_parse_strips_prerelease_with_v() -> None:
    assert _parse_version("v2.0.0-rc.1") == (2, 0, 0)


def test_parse_single_digit() -> None:
    assert _parse_version("1") == (1,)


def test_parse_two_parts() -> None:
    assert _parse_version("0.2") == (0, 2)


# ---------------------------------------------------------------------------
# _is_newer
# ---------------------------------------------------------------------------

def test_is_newer_patch_bump() -> None:
    assert _is_newer("0.1.1", "0.1.0") is True


def test_is_newer_minor_bump() -> None:
    assert _is_newer("0.2.0", "0.1.9") is True


def test_is_newer_major_bump() -> None:
    assert _is_newer("1.0.0", "0.99.99") is True


def test_is_newer_same_version() -> None:
    assert _is_newer("0.1.0", "0.1.0") is False


def test_is_newer_older_remote() -> None:
    assert _is_newer("0.0.9", "0.1.0") is False


def test_is_newer_with_v_prefix_on_remote() -> None:
    assert _is_newer("v0.2.0", "0.1.0") is True


def test_is_newer_prerelease_ignored() -> None:
    # "1.0.0-rc.1" parses as (1, 0, 0) which equals "1.0.0"
    assert _is_newer("1.0.0-rc.1", "1.0.0") is False


def test_is_newer_invalid_remote_returns_false() -> None:
    assert _is_newer("not-a-version", "0.1.0") is False


def test_is_newer_empty_strings_return_false() -> None:
    assert _is_newer("", "0.1.0") is False


# ---------------------------------------------------------------------------
# UpdateInfo
# ---------------------------------------------------------------------------

def test_update_info_frozen() -> None:
    info = UpdateInfo(version="1.0.0", release_url="https://example.com", changelog="## v1.0")
    with pytest.raises((AttributeError, TypeError)):
        info.version = "2.0.0"  # type: ignore[misc]


def test_update_info_fields() -> None:
    info = UpdateInfo(version="2.3.4", release_url="https://example.com/r", changelog="notes")
    assert info.version == "2.3.4"
    assert info.release_url == "https://example.com/r"
    assert info.changelog == "notes"
