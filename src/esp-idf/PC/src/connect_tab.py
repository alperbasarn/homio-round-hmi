# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 02:29:29 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk
import time

# Import the subtab and terminal classes
from serial_subtab import SerialSubTab
from tcp_subtab import TCPSubTab
from mqtt_subtab import MQTTSubTab

class ConnectTab:
    def __init__(self, notebook, app):
        self.app = app
        self.frame = ttk.Frame(notebook)
        
        # Create subtabs
        self.subtabs = ttk.Notebook(self.frame)
        self.subtabs.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Create Serial, TCP, and MQTT subtabs
        self.serial_tab = SerialSubTab(self.subtabs, self.app)
        self.tcp_tab = TCPSubTab(self.subtabs, self.app)
        self.mqtt_tab = MQTTSubTab(self.subtabs, self.app)
        
        self.subtabs.add(self.serial_tab.frame, text="Serial")
        self.subtabs.add(self.tcp_tab.frame, text="TCP")
        self.subtabs.add(self.mqtt_tab.frame, text="MQTT")
        
        # Keep reference to message terminals for centralized logging
        self.terminals = {
            "serial": self.serial_tab.terminal,
            "tcp": self.tcp_tab.terminal,
            "mqtt": self.mqtt_tab.terminal
        }
    
    def log_message(self, message, message_type="status", is_self=False):
        """Log message to appropriate terminal based on type"""
        # Determine which terminal to use based on message type prefix or connection type
        if message_type.startswith("serial") or (message_type in ["sent", "received"] and self.app.connection_type == "serial"):
            terminal = self.serial_tab.terminal
            # Convert generic types to specific terminal types
            if message_type == "sent" and self.app.connection_type == "serial":
                message_type = "serial_sent"
            elif message_type == "received" and self.app.connection_type == "serial":
                message_type = "serial_received"
                
        elif message_type.startswith("tcp") or (message_type in ["sent", "received"] and self.app.connection_type == "tcp"):
            terminal = self.tcp_tab.terminal
            # Convert generic types to specific terminal types
            if message_type == "sent" and self.app.connection_type == "tcp":
                message_type = "tcp_sent"
            elif message_type == "received" and self.app.connection_type == "tcp":
                message_type = "tcp_received"
                
        elif message_type.startswith("mqtt") or message_type == "received":
            terminal = self.mqtt_tab.terminal
            
            # Only filter if it's a self-message AND filtering is enabled
            if is_self and self.mqtt_tab.filter_self_messages.get() and message_type == "mqtt_received":
                # Skip self-messages if filtering is enabled
                return
                
        else:
            # For general status/error messages, determine which tab is active
            try:
                current_tab = self.subtabs.index(self.subtabs.select())
                if current_tab == 0:
                    terminal = self.serial_tab.terminal
                elif current_tab == 1:
                    terminal = self.tcp_tab.terminal
                elif current_tab == 2:
                    terminal = self.mqtt_tab.terminal
                else:
                    # Default to serial if tab can't be determined
                    terminal = self.serial_tab.terminal
            except:
                # Default to serial if tab can't be determined
                terminal = self.serial_tab.terminal
        
        # Log the message using the terminal's method
        try:
            terminal.log_message(message, message_type, is_self=is_self)
        except Exception as e:
            # If there's an error with the terminal's log_message, use a simpler fallback
            print(f"Error logging message: {e}")