"""Windows system volume control and monitoring (pycaw).

Salvaged from the legacy tkinter companion. The tkinter root-marshalling code
was removed: callers now own thread safety (Qt signals are thread-safe; or wrap
the callback in ``QMetaObject.invokeMethod`` if marshalling to the GUI thread).
"""

from __future__ import annotations

import sys
import threading
import time
from collections.abc import Callable

if sys.platform == "win32":
    from ctypes import POINTER, cast

    from comtypes import CLSCTX_ALL  # type: ignore[import-not-found]
    from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume  # type: ignore[import-not-found]


VolumeCallback = Callable[[int], None]


class WindowsAudioController:
    """Read and set the Windows master volume; optionally watch for changes."""

    def __init__(self, volume_callback: VolumeCallback | None = None) -> None:
        if sys.platform != "win32":
            raise RuntimeError("WindowsAudioController is Windows-only")

        devices = AudioUtilities.GetSpeakers()
        interface = devices.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        self._volume = cast(interface, POINTER(IAudioEndpointVolume))
        self._callback = volume_callback
        self._last_volume = self.get_volume_percent()
        self._monitor_thread: threading.Thread | None = None
        self._stop = threading.Event()

    def get_volume_percent(self) -> int:
        """Return current master volume as 0–100."""
        return int(self._volume.GetMasterVolumeLevelScalar() * 100)

    def set_volume_percent(self, percent: int) -> int:
        """Set master volume; returns the clamped value actually applied."""
        clamped = max(0, min(100, percent))
        self._volume.SetMasterVolumeLevelScalar(clamped / 100.0, None)
        self._last_volume = clamped
        return clamped

    def start_monitoring(self, poll_interval_s: float = 0.1) -> None:
        """Spawn a daemon thread that fires the callback when volume changes."""
        if self._monitor_thread is not None and self._monitor_thread.is_alive():
            return
        self._stop.clear()
        self._monitor_thread = threading.Thread(
            target=self._monitor_loop, args=(poll_interval_s,), daemon=True
        )
        self._monitor_thread.start()

    def stop_monitoring(self) -> None:
        self._stop.set()

    def _monitor_loop(self, poll_interval_s: float) -> None:
        while not self._stop.is_set():
            current = self.get_volume_percent()
            if current != self._last_volume:
                self._last_volume = current
                if self._callback is not None:
                    self._callback(current)
            time.sleep(poll_interval_s)
