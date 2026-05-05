# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 07:46:35 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk, scrolledtext
import time

class CommTerminal:
    """Base class for communication terminal interfaces (Serial, TCP, MQTT)"""
    
    def __init__(self, parent_frame, app):
        self.parent_frame = parent_frame
        self.app = app
        
        # Control variables
        self.autoscroll = tk.BooleanVar(value=True)
        self.show_timestamps = tk.BooleanVar(value=True)
        
        # Message display areas
        self.create_message_areas()
        
        # Control panel for terminal options
        self.create_control_panel()
        
        # Command entry area
        self.create_command_area()
    
    def create_message_areas(self):
        """Create message display areas: info, sent, and received messages"""
        messages_frame = ttk.Frame(self.parent_frame)
        messages_frame.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        messages_frame.pack_propagate(False)  # Prevent collapsing when resized
        
        # Info messages area (for status updates, errors, etc.)
        ttk.Label(messages_frame, text="Info Messages:").pack(fill=tk.X, anchor=tk.W)
        self.info_messages = scrolledtext.ScrolledText(messages_frame, height=5, wrap=tk.WORD)
        self.info_messages.pack(fill=tk.X, expand=False, padx=5, pady=5)
        self.info_messages.config(state=tk.DISABLED)
        
        # Sent messages
        ttk.Label(messages_frame, text="Sent Messages:").pack(fill=tk.X, anchor=tk.W)
        self.sent_messages = scrolledtext.ScrolledText(messages_frame, height=10, wrap=tk.WORD)
        self.sent_messages.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.sent_messages.config(state=tk.DISABLED)
        
        # Received messages
        ttk.Label(messages_frame, text="Received Messages:").pack(fill=tk.X, anchor=tk.W)
        self.received_messages = scrolledtext.ScrolledText(messages_frame, height=10, wrap=tk.WORD)
        self.received_messages.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        self.received_messages.config(state=tk.DISABLED)
    
    def create_control_panel(self):
        """Create control panel with buttons for terminal options"""
        control_panel = ttk.Frame(self.parent_frame)
        control_panel.pack(fill=tk.X, padx=5, pady=2)
        
        # Autoscroll toggle
        self.autoscroll_check = ttk.Checkbutton(
            control_panel, 
            text="Autoscroll", 
            variable=self.autoscroll,
            command=self.toggle_autoscroll
        )
        self.autoscroll_check.pack(side=tk.LEFT, padx=5)
        
        # Show/hide timestamps
        self.timestamps_check = ttk.Checkbutton(
            control_panel,
            text="Show Timestamps",
            variable=self.show_timestamps,
            command=self.toggle_timestamps
        )
        self.timestamps_check.pack(side=tk.LEFT, padx=5)
        
        # Clear buttons for each message area
        ttk.Button(
            control_panel,
            text="Clear Info",
            command=lambda: self.clear_messages("info")
        ).pack(side=tk.RIGHT, padx=2)
        
        ttk.Button(
            control_panel,
            text="Clear Received",
            command=lambda: self.clear_messages("received")
        ).pack(side=tk.RIGHT, padx=2)
        
        ttk.Button(
            control_panel,
            text="Clear Sent",
            command=lambda: self.clear_messages("sent")
        ).pack(side=tk.RIGHT, padx=2)
        
        ttk.Button(
            control_panel,
            text="Clear All",
            command=lambda: self.clear_messages("all")
        ).pack(side=tk.RIGHT, padx=2)
    
    def toggle_autoscroll(self):
        """Toggle autoscroll feature"""
        # The BooleanVar handles the state
        pass
    
    def toggle_timestamps(self):
        """Toggle showing timestamps in messages"""
        # Refresh existing messages to apply changes
        # This would require storing messages without timestamps and reapplying
        # For now, just apply to new messages
        pass
    
    def create_command_area(self):
        """Create command entry area"""
        command_frame = ttk.Frame(self.parent_frame)
        command_frame.pack(fill=tk.X, padx=5, pady=5, side=tk.BOTTOM)
        
        ttk.Label(command_frame, text="Command:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.command_entry = ttk.Entry(command_frame, width=70)
        self.command_entry.grid(row=0, column=1, padx=5, pady=5, sticky=tk.EW)
        self.command_entry.bind("<Return>", self.send_command)
        
        self.send_btn = ttk.Button(command_frame, text="Send", command=lambda: self.send_command(None), width=10)
        self.send_btn.grid(row=0, column=2, padx=5, pady=5, sticky=tk.E)
        self.send_btn.config(state=tk.DISABLED)  # Initially disabled
        
        # Make command entry column expandable
        command_frame.grid_columnconfigure(1, weight=1)
    
    def send_command(self, event=None):
        """Base method for sending commands - to be overridden by subclasses"""
        raise NotImplementedError("Subclasses must implement send_command")
        
    def log_message(self, message, message_type="status", is_self=False):
        """Log a message to the appropriate text area"""
        # Add a visual indicator for self-messages
        if is_self and message_type == "mqtt_received":
            message = f"[SELF] {message}"
            
        # Format message with timestamp if enabled
        if self.show_timestamps.get():
            timestamp = time.strftime("%H:%M:%S")
            formatted_message = f"[{timestamp}] {message}\n"
        else:
            formatted_message = f"{message}\n"
        
        # Determine which text area to use
        if message_type in ["sent", "serial_sent", "tcp_sent", "mqtt_sent"]:
            text_area = self.sent_messages
            tag = None
        elif message_type in ["received", "serial_received", "tcp_received", "mqtt_received"]:
            text_area = self.received_messages
            tag = None
        else:  # status, error, info
            text_area = self.info_messages
            tag = message_type if message_type in ["error", "status"] else None
        
        # Update the text area
        text_area.config(state=tk.NORMAL)
        
        if tag:
            text_area.insert(tk.END, formatted_message, tag)
            text_area.tag_config("error", foreground="red")
            text_area.tag_config("status", foreground="blue")
        else:
            text_area.insert(tk.END, formatted_message)
        
        # Scroll to end if autoscroll is enabled
        if self.autoscroll.get():
            text_area.see(tk.END)
            
        text_area.config(state=tk.DISABLED)
    
    def clear_messages(self, message_area="all"):
        """Clear message display areas"""
        if message_area in ["sent", "all"]:
            self.sent_messages.config(state=tk.NORMAL)
            self.sent_messages.delete(1.0, tk.END)
            self.sent_messages.config(state=tk.DISABLED)
            
        if message_area in ["received", "all"]:
            self.received_messages.config(state=tk.NORMAL)
            self.received_messages.delete(1.0, tk.END)
            self.received_messages.config(state=tk.DISABLED)
            
        if message_area in ["info", "all"]:
            self.info_messages.config(state=tk.NORMAL)
            self.info_messages.delete(1.0, tk.END)
            self.info_messages.config(state=tk.DISABLED)


class SerialTerminal(CommTerminal):
    """Terminal for Serial communication"""
    
    def __init__(self, parent_frame, app):
        super().__init__(parent_frame, app)
    
    def send_command(self, event=None):
        """Send a command through the serial port"""
        command = self.command_entry.get()
        if not command:
            return
            
        # Use the app's central command sender
        if self.app.send_command(command):
            # Clear the entry if sent successfully
            self.command_entry.delete(0, tk.END)


class TCPTerminal(CommTerminal):
    """Terminal for TCP communication"""
    
    def __init__(self, parent_frame, app):
        super().__init__(parent_frame, app)
    
    def send_command(self, event=None):
        """Send a command through the TCP socket"""
        command = self.command_entry.get()
        if not command:
            return
            
        # Use the app's central command sender
        if self.app.send_command(command):
            # Clear the entry if sent successfully
            self.command_entry.delete(0, tk.END)


class MQTTTerminal(CommTerminal):
    """Terminal for MQTT communication"""
    
    def __init__(self, parent_frame, app, mqtt_tab):
        super().__init__(parent_frame, app)
        self.mqtt_tab = mqtt_tab
        
        # Modify command area to include topic field
        self.add_topic_field()
    
    def add_topic_field(self):
        """Add MQTT-specific topic field"""
        # Find the command frame (it's the last child of parent_frame)
        command_frames = [f for f in self.parent_frame.winfo_children() 
                         if isinstance(f, ttk.Frame)]
        command_frame = command_frames[-1]
        
        # Remove existing command entry setup
        for widget in command_frame.winfo_children():
            widget.destroy()
            
        # Recreate with topic field
        ttk.Label(command_frame, text="Topic:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.topic_entry = ttk.Entry(command_frame, width=70)
        self.topic_entry.grid(row=0, column=1, padx=5, pady=5, sticky=tk.EW, columnspan=2)
        self.topic_entry.insert(0, "esp32/sound/control")  # Default topic
        
        ttk.Label(command_frame, text="Message:").grid(row=1, column=0, padx=5, pady=5, sticky=tk.W)
        self.command_entry = ttk.Entry(command_frame, width=70)
        self.command_entry.grid(row=1, column=1, padx=5, pady=5, sticky=tk.EW)
        self.command_entry.bind("<Return>", self.send_command)
        
        self.send_btn = ttk.Button(command_frame, text="Send", command=lambda: self.send_command(None), width=10)
        self.send_btn.grid(row=1, column=2, padx=5, pady=5, sticky=tk.E)
        self.send_btn.config(state=tk.DISABLED)  # Initially disabled
        
        # Make the command entry column expandable
        command_frame.grid_columnconfigure(1, weight=1)
    
    def send_command(self, event=None):
        """Send a message to the MQTT broker"""
        if not self.app.mqtt_handler or not self.app.mqtt_handler.is_connected():
            self.log_message("Cannot send message: Not connected to MQTT broker", "error")
            return
        
        topic = self.topic_entry.get()
        message = self.command_entry.get()
        
        if not topic or not message:
            return
        
        # Send message using MQTT handler
        if self.app.mqtt_handler.publish(topic, message):
            # Clear message entry
            self.command_entry.delete(0, tk.END)