# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 03:15:00 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk
import time
from terminal_classes import MQTTTerminal

class MQTTSubTab:
    def __init__(self, notebook, app):
        self.app = app
        self.frame = ttk.Frame(notebook)
        self.filter_self_messages = tk.BooleanVar(value=True)
        self.edit_mode = tk.BooleanVar(value=False)
        self.notebook = notebook
        
        # Create UI elements
        self.create_ui()
        
        # Explicitly set the initial state to ensure parameters are shown
        self.frame.after(100, self.load_config)  # Slight delay to ensure UI is ready
        
        # Bind to tab change event to reload config when this tab is selected
        notebook.bind("<<NotebookTabChanged>>", self.on_tab_changed)
    
    def create_ui(self):
        # Top frame for connection parameters
        params_frame = ttk.LabelFrame(self.frame, text="MQTT Parameters")
        params_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Edit mode checkbox and save button frame
        edit_frame = ttk.Frame(params_frame)
        edit_frame.grid(row=0, column=0, columnspan=4, padx=5, pady=5, sticky=tk.W)
        
        # Edit checkbox
        self.edit_check = ttk.Checkbutton(edit_frame, text="Edit Parameters", variable=self.edit_mode, 
                                         command=self.toggle_edit_mode)
        self.edit_check.pack(side=tk.LEFT, padx=5)
        
        # Save button (initially hidden)
        self.save_btn = ttk.Button(edit_frame, text="Save", command=self.save_parameters)
        
        # Broker URL
        ttk.Label(params_frame, text="Broker URL:").grid(row=1, column=0, padx=5, pady=5, sticky=tk.W)
        self.broker_entry = ttk.Entry(params_frame, width=40, state=tk.DISABLED)
        self.broker_entry.grid(row=1, column=1, padx=5, pady=5, sticky=tk.W)
        
        # Port
        ttk.Label(params_frame, text="Port:").grid(row=1, column=2, padx=5, pady=5, sticky=tk.W)
        self.port_entry = ttk.Entry(params_frame, width=6, state=tk.DISABLED)
        self.port_entry.grid(row=1, column=3, padx=5, pady=5, sticky=tk.W)
        
        # Username
        ttk.Label(params_frame, text="Username:").grid(row=2, column=0, padx=5, pady=5, sticky=tk.W)
        self.username_entry = ttk.Entry(params_frame, width=20, state=tk.DISABLED)
        self.username_entry.grid(row=2, column=1, padx=5, pady=5, sticky=tk.W)
        
        # Password
        ttk.Label(params_frame, text="Password:").grid(row=2, column=2, padx=5, pady=5, sticky=tk.W)
        self.password_entry = ttk.Entry(params_frame, width=20, show="*", state=tk.DISABLED)
        self.password_entry.grid(row=2, column=3, padx=5, pady=5, sticky=tk.W)
        
        # Control buttons frame
        control_frame = ttk.Frame(self.frame)
        control_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Connect button
        self.connect_btn = ttk.Button(control_frame, text="Connect MQTT", command=self.connect_mqtt)
        self.connect_btn.pack(side=tk.LEFT, padx=5, pady=5)
        
        # Disconnect button
        self.disconnect_btn = ttk.Button(control_frame, text="Disconnect MQTT", command=self.disconnect_mqtt, state=tk.DISABLED)
        self.disconnect_btn.pack(side=tk.LEFT, padx=5, pady=5)
        
        # Self-message filter checkbox
        self.filter_check = ttk.Checkbutton(control_frame, text="Filter self messages", 
                                           variable=self.filter_self_messages,
                                           command=self.toggle_filter)
        self.filter_check.pack(side=tk.RIGHT, padx=5, pady=5)
        
        # Status display
        self.status_label = ttk.Label(control_frame, text="Status: Disconnected", foreground="red")
        self.status_label.pack(side=tk.LEFT, padx=20, pady=5)
        
        # Create MQTT terminal using the MQTTTerminal class
        self.terminal = MQTTTerminal(self.frame, self.app, self)
    
    def load_config(self):
        """Load MQTT parameters from configuration file"""
        if hasattr(self.app, 'config_handler'):
            mqtt_config = self.app.config_handler.get_mqtt_config()
            
            # Set UI fields
            self.broker_entry.config(state=tk.NORMAL)  # Temporarily enable for updating
            self.port_entry.config(state=tk.NORMAL)
            self.username_entry.config(state=tk.NORMAL)
            self.password_entry.config(state=tk.NORMAL)
            
            self.broker_entry.delete(0, tk.END)
            self.broker_entry.insert(0, mqtt_config.get("broker", ""))
            
            self.port_entry.delete(0, tk.END)
            self.port_entry.insert(0, str(mqtt_config.get("port", "")))
            
            self.username_entry.delete(0, tk.END)
            self.username_entry.insert(0, mqtt_config.get("username", ""))
            
            self.password_entry.delete(0, tk.END)
            # Show asterisks for password when in display mode
            if not self.edit_mode.get():
                self.password_entry.insert(0, "**************")
            else:
                self.password_entry.insert(0, mqtt_config.get("password", ""))
            
            # Reset state based on edit mode
            if not self.edit_mode.get():
                self.broker_entry.config(state=tk.DISABLED)
                self.port_entry.config(state=tk.DISABLED)
                self.username_entry.config(state=tk.DISABLED)
                self.password_entry.config(state=tk.DISABLED)
            
            # Update MQTT handler
            if hasattr(self.app, 'mqtt_handler'):
                self.app.mqtt_handler.set_broker(
                    mqtt_config.get("broker", ""),
                    mqtt_config.get("port", 8883),
                    mqtt_config.get("username", ""),
                    mqtt_config.get("password", "")
                )
    
    def toggle_edit_mode(self):
        """Toggle edit mode for parameters"""
        edit_mode = self.edit_mode.get()
        
        if edit_mode:
            # Enable fields and show save button
            self.set_parameters_editable(True)
            self.save_btn.pack(side=tk.LEFT, padx=5)
            
            # Show actual password and ensure parameters are visible
            if hasattr(self.app, 'config_handler'):
                mqtt_config = self.app.config_handler.get_mqtt_config()
                
                # Make sure broker and other fields are showing current values
                self.broker_entry.delete(0, tk.END)
                self.broker_entry.insert(0, mqtt_config.get("broker", ""))
                
                self.port_entry.delete(0, tk.END)
                self.port_entry.insert(0, str(mqtt_config.get("port", "")))
                
                self.username_entry.delete(0, tk.END)
                self.username_entry.insert(0, mqtt_config.get("username", ""))
                
                # Update password
                self.password_entry.delete(0, tk.END)
                self.password_entry.insert(0, mqtt_config.get("password", ""))
        else:
            # Disable fields and hide save button
            self.set_parameters_editable(False)
            self.save_btn.pack_forget()
            
            # Load config (will reset fields and show asterisks for password)
            self.load_config()
    
    def set_parameters_editable(self, editable):
        """Enable or disable parameter fields"""
        state = tk.NORMAL if editable else tk.DISABLED
        self.broker_entry.config(state=state)
        self.port_entry.config(state=state)
        self.username_entry.config(state=state)
        self.password_entry.config(state=state)
    
    def save_parameters(self):
        """Save MQTT parameters to config file"""
        broker = self.broker_entry.get()
        
        try:
            port = int(self.port_entry.get())
        except ValueError:
            port = 8883  # Default if invalid
        
        username = self.username_entry.get()
        password = self.password_entry.get()
        
        # Don't save if password is masked
        if password == "**************":
            # Get original password from config
            if hasattr(self.app, 'config_handler'):
                mqtt_config = self.app.config_handler.get_mqtt_config()
                password = mqtt_config.get("password", "")
        
        # Save to config file
        if hasattr(self.app, 'config_handler'):
            self.app.config_handler.save_mqtt_config(broker, port, username, password)
            
            # Update MQTT handler with new parameters
            if hasattr(self.app, 'mqtt_handler'):
                self.app.mqtt_handler.set_broker(broker, port, username, password)
            
            # Log the action
            self.terminal.log_message(f"MQTT parameters saved to config file", "status")
        
        # Turn off edit mode
        self.edit_mode.set(False)
        self.toggle_edit_mode()
    
    def connect_mqtt(self):
        """Connect to MQTT broker using current parameters"""
        if self.app.mqtt_handler:
            if self.app.mqtt_handler.connect():
                # Update UI
                self.status_label.config(text="Status: Connected", foreground="green")
                self.connect_btn.config(state=tk.DISABLED)
                self.disconnect_btn.config(state=tk.NORMAL)
                self.terminal.send_btn.config(state=tk.NORMAL)
                
                # Removed duplicate log message
            else:
                self.terminal.log_message("Failed to connect to MQTT broker", "error")
    
    def disconnect_mqtt(self):
        """Disconnect from MQTT broker"""
        if self.app.mqtt_handler:
            self.app.mqtt_handler.disconnect()
            
            # Update UI
            self.status_label.config(text="Status: Disconnected", foreground="red")
            self.connect_btn.config(state=tk.NORMAL)
            self.disconnect_btn.config(state=tk.DISABLED)
            self.terminal.send_btn.config(state=tk.DISABLED)
            
            # Log disconnection
            self.terminal.log_message("Disconnected from MQTT broker", "status")
            
    def toggle_filter(self):
        """Called when filter checkbox is toggled"""
        # Force an update in case there are already received messages shown/hidden
        filter_state = self.filter_self_messages.get()
        self.terminal.log_message(
            f"Self-message filtering {'enabled' if filter_state else 'disabled'}", 
            "status"
        )
    
    def update_mqtt_status(self, connected, broker=""):
        """Update MQTT connection status display"""
        if connected:
            self.status_label.config(text=f"Status: Connected to {broker}", foreground="green")
            self.connect_btn.config(state=tk.DISABLED)
            self.disconnect_btn.config(state=tk.NORMAL)
            self.terminal.send_btn.config(state=tk.NORMAL)
            
            # Notify filter state change to ensure messages are displayed correctly
            self.filter_self_messages.set(self.filter_self_messages.get())
        else:
            self.status_label.config(text="Status: Disconnected", foreground="red")
            self.connect_btn.config(state=tk.NORMAL)
            self.disconnect_btn.config(state=tk.DISABLED)
            self.terminal.send_btn.config(state=tk.DISABLED)
    
    def on_tab_changed(self, event):
        """Handle tab change events to load config when this tab is selected"""
        try:
            selected_tab = self.notebook.index(self.notebook.select())
            # Find which index this tab is
            for i in range(self.notebook.index("end")):
                if self.notebook.winfo_children()[i] == self.frame:
                    my_index = i
                    break
            else:
                return  # Tab not found
                
            # If this tab is selected, reload config
            if selected_tab == my_index:
                self.load_config()
                
                # Ensure the command frame is visible
                self.frame.update_idletasks()
                self.frame.config(height=400)  # Set a minimum height
                
                # Don't refresh messages when switching tabs
        except Exception as e:
            print(f"Error in MQTT tab change handler: {e}")