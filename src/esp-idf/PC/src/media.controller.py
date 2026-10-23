# -*- coding: utf-8 -*-
"""
Created on Thu Mar 27 05:55:18 2025

@author: Alper Basaran
"""
import win32gui
import win32api
import win32con
import time
import re

class MediaController:
    """Detects and controls media playback in common applications like YouTube, Spotify, etc."""
    
    def __init__(self):
        # Define patterns to identify media players by window title
        self.media_player_patterns = [
            # YouTube in browser windows
            re.compile(r'.*YouTube.*', re.IGNORECASE),  
            # Spotify window
            re.compile(r'Spotify.*', re.IGNORECASE),     
            # Windows Media Player
            re.compile(r'Windows Media Player', re.IGNORECASE),  
            # VLC Media Player
            re.compile(r'VLC media player', re.IGNORECASE),   
            # iTunes
            re.compile(r'iTunes', re.IGNORECASE), 
            # Netflix
            re.compile(r'Netflix.*', re.IGNORECASE),   
            # Amazon Prime Video
            re.compile(r'Prime Video.*', re.IGNORECASE),  
            # Disney+
            re.compile(r'Disney\+.*', re.IGNORECASE)
        ]
        
        # Cached window handles
        self.media_windows = []
        self.last_check_time = 0
        self.refresh_interval = 5  # seconds
        
        # Keep track of currently detected player
        self.current_player = None
        
        # Track playing state
        self.is_playing = False
    
    def get_all_windows(self):
        """Get all visible window handles and titles"""
        result = []
        
        def enum_callback(hwnd, results):
            if win32gui.IsWindowVisible(hwnd):
                title = win32gui.GetWindowText(hwnd)
                if title:  # Only include windows with titles
                    results.append((hwnd, title))
        
        win32gui.EnumWindows(enum_callback, result)
        return result
    
    def find_media_windows(self, force_refresh=False):
        """Find windows that match media player patterns"""
        current_time = time.time()
        
        # Only refresh window list if needed
        if force_refresh or len(self.media_windows) == 0 or (current_time - self.last_check_time > self.refresh_interval):
            all_windows = self.get_all_windows()
            self.media_windows = []
            
            # Find windows that match media player patterns
            for hwnd, title in all_windows:
                for pattern in self.media_player_patterns:
                    if pattern.match(title):
                        self.media_windows.append((hwnd, title))
                        break
            
            self.last_check_time = current_time
        
        return self.media_windows
    
    def detect_media_state(self):
        """Detect if media is playing by checking window titles"""
        windows = self.find_media_windows(force_refresh=True)
        
        # These are patterns that indicate media is playing (often shown in window title)
        playing_indicators = [
            # Common in browser window titles when playing video
            "▶", 
            # Often used to indicate playing in window titles
            "Playing", 
            # YouTube uses this symbol when a video is playing
            "- YouTube"
        ]
        
        for hwnd, title in windows:
            for indicator in playing_indicators:
                if indicator in title:
                    self.current_player = title
                    self.is_playing = True
                    return True
        
        # If we have media windows but no playing indicators, assume paused
        if windows:
            self.current_player = windows[0][1]  # Use the first one
            self.is_playing = False
            return False
        
        # No media windows detected
        self.current_player = None
        self.is_playing = False
        return False
    
    def send_media_key(self, key_code):
        """Send a media control key to the system"""
        # Virtual Key Codes for media control
        # VK_MEDIA_PLAY_PAUSE = 0xB3
        # VK_MEDIA_STOP = 0xB2
        # VK_MEDIA_PREV_TRACK = 0xB1
        # VK_MEDIA_NEXT_TRACK = 0xB0
        
        # Send the key to the system (global hotkey)
        win32api.keybd_event(key_code, 0, win32con.KEYEVENTF_EXTENDEDKEY, 0)
        time.sleep(0.1)
        win32api.keybd_event(key_code, 0, win32con.KEYEVENTF_EXTENDEDKEY | win32con.KEYEVENTF_KEYUP, 0)
    
    def play_pause(self):
        """Toggle between play and pause"""
        # Virtual key code for play/pause
        self.send_media_key(0xB3)  # VK_MEDIA_PLAY_PAUSE
        time.sleep(0.2)  # Give time for state to change
        self.detect_media_state()  # Update state after attempting to control
        return self.is_playing
    
    def next_track(self):
        """Go to next track/forward"""
        self.send_media_key(0xB0)  # VK_MEDIA_NEXT_TRACK
        return True
    
    def prev_track(self):
        """Go to previous track/rewind"""
        self.send_media_key(0xB1)  # VK_MEDIA_PREV_TRACK
        return True
    
    def get_playing_state(self):
        """Get current playing state"""
        self.detect_media_state()
        return self.is_playing
    
    def get_current_player(self):
        """Get name of current detected media player"""
        return self.current_player

