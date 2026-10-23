from ctypes import cast, POINTER
from comtypes import CLSCTX_ALL
from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume
import threading
import time
import tkinter as tk

class WindowsAudioController:
    """Controls and monitors Windows system volume"""
    
    def __init__(self, volume_callback=None):
        self.devices = AudioUtilities.GetSpeakers()
        self.interface = self.devices.Activate(
            IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
        self.volume = cast(self.interface, POINTER(IAudioEndpointVolume))
        self.callback = volume_callback
        self.last_volume = self.get_volume_percent()
        self.monitoring = False
        self.root = None  # Will be set by set_root
        
    def set_root(self, root):
        """Set the tkinter root for thread-safe callbacks"""
        self.root = root
    
    def get_volume_percent(self):
        """Get current volume as percentage (0-100)"""
        # GetMasterVolumeLevelScalar returns 0.0 to 1.0
        vol = self.volume.GetMasterVolumeLevelScalar() * 100
        return int(vol)
    
    def set_volume_percent(self, percent):
        """Set volume as percentage (0-100)"""
        # Ensure volume is between 0 and 100
        percent = max(0, min(100, percent))
        # SetMasterVolumeLevelScalar expects 0.0 to 1.0
        self.volume.SetMasterVolumeLevelScalar(percent / 100.0, None)
        self.last_volume = percent
        return percent
    
    def start_monitoring(self):
        """Start a thread to monitor volume changes"""
        if not self.monitoring:
            self.monitoring = True
            threading.Thread(target=self._monitor_volume, daemon=True).start()
    
    def stop_monitoring(self):
        """Stop monitoring volume changes"""
        self.monitoring = False
    
    def _monitor_volume(self):
        """Thread function to monitor volume changes"""
        while self.monitoring:
            current_vol = self.get_volume_percent()
            if current_vol != self.last_volume:
                if self.callback:
                    # Use thread-safe method to update UI
                    if self.root:
                        self.root.after(0, lambda vol=current_vol: self.callback(vol))
                    else:
                        # If root not set, just call directly (not thread-safe for UI)
                        self.callback(current_vol)
                self.last_volume = current_vol
            time.sleep(0.1)  # Check every 100ms