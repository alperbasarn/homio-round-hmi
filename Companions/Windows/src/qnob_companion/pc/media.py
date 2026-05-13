"""Detect and control PC media playback via Windows virtual key codes.

Salvaged from the legacy tkinter companion (no functional changes beyond a
small cleanup pass). Windows-only.
"""

from __future__ import annotations

import re
import sys
import time

if sys.platform == "win32":
    import win32api  # type: ignore[import-not-found]
    import win32con  # type: ignore[import-not-found]
    import win32gui  # type: ignore[import-not-found]


# Patterns that identify common media player windows by title.
_MEDIA_PLAYER_PATTERNS: tuple[re.Pattern[str], ...] = (
    re.compile(r".*YouTube.*", re.IGNORECASE),
    re.compile(r"Spotify.*", re.IGNORECASE),
    re.compile(r"Windows Media Player", re.IGNORECASE),
    re.compile(r"VLC media player", re.IGNORECASE),
    re.compile(r"iTunes", re.IGNORECASE),
    re.compile(r"Netflix.*", re.IGNORECASE),
    re.compile(r"Prime Video.*", re.IGNORECASE),
    re.compile(r"Disney\+.*", re.IGNORECASE),
)

# Substrings in window titles that suggest media is currently playing.
_PLAYING_INDICATORS: tuple[str, ...] = ("▶", "Playing", "- YouTube")

# Virtual key codes for media keys (winuser.h).
VK_MEDIA_PLAY_PAUSE = 0xB3
VK_MEDIA_NEXT_TRACK = 0xB0
VK_MEDIA_PREV_TRACK = 0xB1


class MediaController:
    """Detects and controls media playback in common applications."""

    def __init__(self, refresh_interval_s: float = 5.0) -> None:
        if sys.platform != "win32":
            raise RuntimeError("MediaController is Windows-only")
        self._media_windows: list[tuple[int, str]] = []
        self._last_check = 0.0
        self._refresh_interval = refresh_interval_s
        self.current_player: str | None = None
        self.is_playing: bool = False

    def _get_all_windows(self) -> list[tuple[int, str]]:
        result: list[tuple[int, str]] = []

        def enum_callback(hwnd: int, _: object) -> None:
            if win32gui.IsWindowVisible(hwnd):
                title = win32gui.GetWindowText(hwnd)
                if title:
                    result.append((hwnd, title))

        win32gui.EnumWindows(enum_callback, None)
        return result

    def find_media_windows(self, *, force_refresh: bool = False) -> list[tuple[int, str]]:
        now = time.time()
        if (
            force_refresh
            or not self._media_windows
            or (now - self._last_check) > self._refresh_interval
        ):
            self._media_windows = [
                (hwnd, title)
                for hwnd, title in self._get_all_windows()
                if any(p.match(title) for p in _MEDIA_PLAYER_PATTERNS)
            ]
            self._last_check = now
        return self._media_windows

    def detect_media_state(self) -> bool:
        """Update ``is_playing`` and ``current_player`` from window titles."""
        windows = self.find_media_windows(force_refresh=True)

        for _hwnd, title in windows:
            if any(ind in title for ind in _PLAYING_INDICATORS):
                self.current_player = title
                self.is_playing = True
                return True

        if windows:
            self.current_player = windows[0][1]
            self.is_playing = False
            return False

        self.current_player = None
        self.is_playing = False
        return False

    def _send_media_key(self, key_code: int) -> None:
        win32api.keybd_event(key_code, 0, win32con.KEYEVENTF_EXTENDEDKEY, 0)
        time.sleep(0.1)
        win32api.keybd_event(
            key_code, 0, win32con.KEYEVENTF_EXTENDEDKEY | win32con.KEYEVENTF_KEYUP, 0
        )

    def play_pause(self) -> bool:
        self._send_media_key(VK_MEDIA_PLAY_PAUSE)
        time.sleep(0.2)
        self.detect_media_state()
        return self.is_playing

    def next_track(self) -> None:
        self._send_media_key(VK_MEDIA_NEXT_TRACK)

    def prev_track(self) -> None:
        self._send_media_key(VK_MEDIA_PREV_TRACK)
