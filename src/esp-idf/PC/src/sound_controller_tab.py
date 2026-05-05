# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 02:32:18 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk

class SoundControllerTab:
    def __init__(self, notebook, app):
        self.app = app
        self.frame = ttk.Frame(notebook)
        self.isPlaying = False
        
        # Create UI elements
        self.create_ui()
    
    def create_ui(self):
        # Volume control section
        volume_frame = ttk.LabelFrame(self.frame, text="Volume Control")
        volume_frame.pack(fill=tk.X, padx=10, pady=5)
        
        # Volume label
        self.volume_label = ttk.Label(volume_frame, text="Volume: 50%")
        self.volume_label.pack(pady=5)
        
        # Volume slider
        self.volume_slider = ttk.Scale(volume_frame, from_=0, to=100, orient=tk.HORIZONTAL,
                                      command=self.on_volume_slider_change)
        self.volume_slider.pack(fill=tk.X, padx=10, pady=5)
        self.volume_slider.set(50)  # Default value
        
        # Media control section
        media_frame = ttk.LabelFrame(self.frame, text="Media Control")
        media_frame.pack(fill=tk.X, padx=10, pady=5)
        
        # Media control buttons
        buttons_frame = ttk.Frame(media_frame)
        buttons_frame.pack(pady=10)
        
        # Get button styles
        style = ttk.Style()
        style.configure("Large.TButton", font=('Helvetica', 14))
        
        # Rewind button
        self.rewind_btn = ttk.Button(buttons_frame, text="◀◀ Rewind", 
                                     command=lambda: self.send_media_command("rewind"),
                                     style="Large.TButton")
        self.rewind_btn.grid(row=0, column=0, padx=10)
        
        # Play button
        self.play_btn = ttk.Button(buttons_frame, text="▶ Play", 
                                  command=self.toggle_play_pause,
                                  style="Large.TButton")
        self.play_btn.grid(row=0, column=1, padx=10)
        
        # Pause button - initially hidden
        self.pause_btn = ttk.Button(buttons_frame, text="⏸ Pause", 
                                   command=self.toggle_play_pause,
                                   style="Large.TButton")
        self.pause_btn.grid(row=0, column=1, padx=10)
        self.pause_btn.grid_remove()  # Hide initially
        
        # Forward button
        self.forward_btn = ttk.Button(buttons_frame, text="▶▶ Forward", 
                                     command=lambda: self.send_media_command("forward"),
                                     style="Large.TButton")
        self.forward_btn.grid(row=0, column=2, padx=10)
        
        # Status display
        status_frame = ttk.Frame(media_frame)
        status_frame.pack(pady=5, fill=tk.X)
        
        # Media status
        status_label = ttk.Label(status_frame, text="Media Status:")
        status_label.pack(side=tk.LEFT, padx=10)
        
        self.status_display = ttk.Label(status_frame, text="Paused", foreground="red")
        self.status_display.pack(side=tk.LEFT)
        
        # Detected player
        player_label = ttk.Label(status_frame, text="    Detected Player:")
        player_label.pack(side=tk.LEFT, padx=10)
        
        self.player_display = ttk.Label(status_frame, text="None", foreground="gray")
        self.player_display.pack(side=tk.LEFT)
    
    def on_volume_slider_change(self, value):
        """Handle volume slider change"""
        volume = int(float(value))
        self.volume_label.config(text=f"Volume: {volume}%")
        
        # Update the Windows audio through the app's audio controller
        self.app.audio_controller.set_volume_percent(volume)
    
    def update_volume(self, volume, send_to_device=True):
        """Update the volume display and slider"""
        self.volume_label.config(text=f"Volume: {volume}%")
        self.volume_slider.set(volume)
        
        # If requested, send the volume to the connected device
        if send_to_device and (self.app.serial_connected or self.app.tcp_connected):
            self.app.send_command(f"setpoint={volume}")
    
    def toggle_play_pause(self):
        """Toggle between play and pause"""
        if self.isPlaying:
            self.send_media_command("pause")
        else:
            self.send_media_command("play")
    
    def send_media_command(self, command):
        """Send a media control command"""
        # Control local media first
        if command == "play":
            self.app.media_controller.play_pause()
            # Wait to see if play succeeded before updating UI
            is_playing = self.app.media_controller.get_playing_state()
            if is_playing:
                self.update_playing_state(True)
        elif command == "pause":
            self.app.media_controller.play_pause()
            # Force pause state
            self.update_playing_state(False)
        elif command == "rewind":
            self.app.media_controller.prev_track()
        elif command == "forward":
            self.app.media_controller.next_track()
        
        # If connected, send command to device
        if self.app.serial_connected or self.app.tcp_connected:
            self.app.send_command(command)
    
    def update_playing_state(self, is_playing):
        """Update the play/pause button and status based on playing state"""
        self.isPlaying = is_playing
        
        if is_playing:
            # Switch to pause button
            self.play_btn.grid_remove()
            self.pause_btn.grid()
            self.status_display.config(text="Playing", foreground="green")
        else:
            # Switch to play button
            self.pause_btn.grid_remove()
            self.play_btn.grid()
            self.status_display.config(text="Paused", foreground="red")
    
    def update_player_info(self, player_name):
        """Update the detected player display"""
        if player_name:
            # Truncate if too long
            if len(player_name) > 30:
                player_name = player_name[:27] + "..."
            self.player_display.config(text=player_name, foreground="blue")
        else:
            self.player_display.config(text="None", foreground="gray")