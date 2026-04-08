# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 02:31:59 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk, messagebox
import json
import re

class ConfigureTab:
    def __init__(self, notebook, app):
        self.app = app
        self.frame = ttk.Frame(notebook)
        self.loading = False 
        # Dictionary to track EEPROM values
        self.eeprom_values = {}
        self.original_values = {}
        self.eeprom_fields = {}
        self.modified = False
        self.loading = False  # Add a loading flag to prevent multiple requests
        
        # Create UI elements
        self.create_ui()

    
    def create_ui(self):
        # Top frame for load/save buttons
        button_frame = ttk.Frame(self.frame)
        button_frame.pack(fill=tk.X, padx=5, pady=5)
        
        self.load_btn = ttk.Button(button_frame, text="Load from Device", command=self.load_configuration)
        self.load_btn.pack(side=tk.LEFT, padx=5, pady=5)
        
        self.save_btn = ttk.Button(button_frame, text="Save to Device", command=self.save_configuration, state=tk.DISABLED)
        self.save_btn.pack(side=tk.LEFT, padx=5, pady=5)
        
        # Status label
        self.status_label = ttk.Label(button_frame, text="")
        self.status_label.pack(side=tk.LEFT, padx=20, pady=5, fill=tk.X, expand=True)
        
        # Scrollable frame for configuration fields
        self.canvas = tk.Canvas(self.frame)
        scrollbar = ttk.Scrollbar(self.frame, orient="vertical", command=self.canvas.yview)
        self.scrollable_frame = ttk.Frame(self.canvas)
        
        self.scrollable_frame.bind(
            "<Configure>",
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all"))
        )
        
        self.canvas.create_window((0, 0), window=self.scrollable_frame, anchor="nw")
        self.canvas.configure(yscrollcommand=scrollbar.set)
        
        self.canvas.pack(side="left", fill="both", expand=True)
        scrollbar.pack(side="right", fill="y")
        
        # Create field containers to group related settings
        self.wifi_frame = self.create_section_frame("WiFi Settings", 0)
        self.mqtt_frame = self.create_section_frame("MQTT Settings", 1)
        self.device_frame = self.create_section_frame("Device Settings", 2)
        self.misc_frame = self.create_section_frame("Miscellaneous Settings", 3)
        
        # Add a stretch frame at the bottom to push everything up
        stretch_frame = ttk.Frame(self.scrollable_frame)
        stretch_frame.grid(row=4, column=0, sticky="nsew", pady=20)
        self.scrollable_frame.grid_rowconfigure(4, weight=1)
    
    def create_section_frame(self, title, row):
        """Create a labeled frame for a section of settings"""
        section = ttk.LabelFrame(self.scrollable_frame, text=title)
        section.grid(row=row, column=0, sticky="ew", padx=10, pady=5, ipadx=5, ipady=5)
        
        # Make sure the section expands to fill the width
        self.scrollable_frame.grid_columnconfigure(0, weight=1)
        
        return section
    
    def create_field(self, parent, label, key, row, value_type="string", options=None):
        """Create a labeled field for a configuration value"""
        ttk.Label(parent, text=label).grid(row=row, column=0, padx=5, pady=2, sticky=tk.W)
        
        # Create different input widgets based on value type
        if value_type == "boolean":
            var = tk.BooleanVar(value=False)
            field = ttk.Checkbutton(parent, variable=var)
            field.var = var  # Store reference to the variable
        elif value_type == "dropdown" and options:
            var = tk.StringVar(value=options[0] if options else "")
            field = ttk.Combobox(parent, values=options, textvariable=var)
            field.var = var  # Store reference to the variable
            field.state(["readonly"])  # Make dropdown readonly
        else:  # Default to string input
            var = tk.StringVar(value="")
            field = ttk.Entry(parent, textvariable=var)
            field.var = var  # Store reference to the variable
        
        field.grid(row=row, column=1, padx=5, pady=2, sticky=tk.EW)
        
        # Make the entry column expandable
        parent.grid_columnconfigure(1, weight=1)
        
        # Bind modification tracking
        if value_type == "boolean":
            var.trace_add("write", lambda *args, key=key: self.value_changed(key))
        else:
            field.bind("<KeyRelease>", lambda event, key=key: self.value_changed(key))
            if value_type == "dropdown":
                field.bind("<<ComboboxSelected>>", lambda event, key=key: self.value_changed(key))
        
        # Store the field reference
        self.eeprom_fields[key] = {"widget": field, "type": value_type}
        
        return field
        
    def load_configuration(self):
        """Request configuration from the device"""
        if not self.app.serial_connected and not self.app.tcp_connected:
            messagebox.showerror("Error", "Not connected to a device")
            return
        
        # Prevent multiple loads
        if self.loading:
            return
        
        self.loading = True
        
        # Clear any existing fields first
        self.clear_fields()
        
        # Request NVS values using listNVSValues command
        self.app.send_command("listNVSValues")
        
        # Update status
        self.status_label.config(text="Requested configuration from device")
    def clear_fields(self):
        """Clear all configuration fields"""
        # Clear existing fields from the frames
        for widget in self.wifi_frame.winfo_children():
            widget.destroy()
        for widget in self.mqtt_frame.winfo_children():
            widget.destroy()
        for widget in self.device_frame.winfo_children():
            widget.destroy()
        for widget in self.misc_frame.winfo_children():
            widget.destroy()
        
        # Reset tracking dictionaries
        self.eeprom_values = {}
        self.original_values = {}
        self.eeprom_fields = {}
        self.modified = False
        self.save_btn.config(state=tk.DISABLED)
    
    def populate_test_data(self):
        """Populate with test data for UI development"""
        # WiFi settings
        self.create_field(self.wifi_frame, "WiFi SSID (Slot 0):", "wifi0_ssid", 0)
        self.update_field_value("wifi0_ssid", "MyNetwork")
        
        self.create_field(self.wifi_frame, "WiFi Password (Slot 0):", "wifi0_password", 1)
        self.update_field_value("wifi0_password", "password123")
        
        # MQTT settings
        self.create_field(self.mqtt_frame, "Sound MQTT Server:", "sound_mqtt_server", 0)
        self.update_field_value("sound_mqtt_server", "mqtt.example.com")
        
        self.create_field(self.mqtt_frame, "Sound MQTT Port:", "sound_mqtt_port", 1)
        self.update_field_value("sound_mqtt_port", "8883")
        
        # Device settings
        self.create_field(self.device_frame, "Device Name:", "device_name", 0)
        self.update_field_value("device_name", "MyQNOB")
        
        # Set original values to match current values
        self.original_values = self.eeprom_values.copy()
    
    def update_field_value(self, key, value):
        """Update a field's value"""
        if key in self.eeprom_fields:
            field = self.eeprom_fields[key]
            widget = field["widget"]
            field_type = field["type"]
            
            # Update the widget based on its type
            if field_type == "boolean":
                # Convert value to boolean and update
                bool_value = value.lower() in ("true", "yes", "1", "on")
                widget.var.set(bool_value)
            elif field_type == "dropdown":
                # Set dropdown value
                if value in widget["values"]:
                    widget.set(value)
                else:
                    widget.current(0)
            else: 
                # Standard string field
                widget.var.set(value)
            
            # Store the value
            self.eeprom_values[key] = value
        
    def process_command_response(self, response):
        """Process a configuration response from the device"""
        # Check if this is a response to listNVSValues command
        if "=" in response:
            # Parse key=value pairs
            key_value = response.split('=', 1)
            if len(key_value) == 2:
                key = key_value[0].strip()
                value = key_value[1].strip()
                
                # Process based on key type
                if key.startswith("wifi") and key.endswith("_ssid"):
                    # Handle WiFi SSID
                    slot = key[4:5]  # Extract slot number
                    ssid_key = f"wifi{slot}_ssid"
                    pass_key = f"wifi{slot}_password"
                    
                    if ssid_key not in self.eeprom_fields:
                        self.create_field(self.wifi_frame, f"WiFi SSID (Slot {slot}):", ssid_key, int(slot)*2)
                    if pass_key not in self.eeprom_fields:
                        self.create_field(self.wifi_frame, f"WiFi Password (Slot {slot}):", pass_key, int(slot)*2+1)
                    
                    self.update_field_value(ssid_key, value)
                    
                elif key.startswith("wifi") and key.endswith("_password"):
                    # Handle WiFi password
                    self.update_field_value(key, "********")
                
                elif key == "lastConnectedNetwork":
                    # Handle last connected WiFi slot
                    slot_key = "last_wifi_slot"
                    
                    if slot_key not in self.eeprom_fields:
                        self.create_field(self.wifi_frame, "Last Used WiFi Slot:", slot_key, 6, 
                                         value_type="dropdown", options=["0", "1", "2"])
                    
                    self.update_field_value(slot_key, value)
                
                elif key == "deviceName":
                    # Handle device name
                    device_key = "device_name"
                    if device_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Device Name:", device_key, 0)
                    self.update_field_value(device_key, value)
                
                elif key.startswith("sound") and key.endswith("URL"):
                    # Handle Sound MQTT URL
                    mqtt_key = "sound_mqtt_url"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT URL:", mqtt_key, 0)
                    self.update_field_value(mqtt_key, value)
                
                elif key.startswith("sound") and key.endswith("Port"):
                    # Handle Sound MQTT Port
                    mqtt_key = "sound_mqtt_port" 
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT Port:", mqtt_key, 1)
                    self.update_field_value(mqtt_key, value)
                
                elif key.startswith("sound") and key.endswith("Username"):
                    # Handle Sound MQTT Username
                    mqtt_key = "sound_mqtt_username"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT Username:", mqtt_key, 2)
                    self.update_field_value(mqtt_key, value)
                
                elif key.startswith("sound") and key.endswith("Password"):
                    # Handle Sound MQTT Password (masked)
                    mqtt_key = "sound_mqtt_password"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT Password:", mqtt_key, 3)
                    self.update_field_value(mqtt_key, "********")
                
                elif key.startswith("light") and key.endswith("URL"):
                    # Handle Light MQTT URL
                    mqtt_key = "light_mqtt_url"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT URL:", mqtt_key, 4)
                    self.update_field_value(mqtt_key, value)
                
                elif key.startswith("light") and key.endswith("Port"):
                    # Handle Light MQTT Port
                    mqtt_key = "light_mqtt_port"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT Port:", mqtt_key, 5)
                    self.update_field_value(mqtt_key, value)
                
                elif key.startswith("light") and key.endswith("Username"):
                    # Handle Light MQTT Username
                    mqtt_key = "light_mqtt_username"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT Username:", mqtt_key, 6)
                    self.update_field_value(mqtt_key, value)
                
                elif key.startswith("light") and key.endswith("Password"):
                    # Handle Light MQTT Password (masked)
                    mqtt_key = "light_mqtt_password"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT Password:", mqtt_key, 7)
                    self.update_field_value(mqtt_key, "********")
                
                elif key == "staticIPEnabled":
                    # Handle Static IP Enabled flag
                    ip_key = "static_ip_enabled"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static IP Enabled:", ip_key, 6, value_type="boolean")
                    self.update_field_value(ip_key, value)
                
                elif key == "staticIP":
                    # Handle Static IP
                    ip_key = "static_ip"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static IP:", ip_key, 7)
                    self.update_field_value(ip_key, value)
                
                elif key == "staticGateway":
                    # Handle Static Gateway
                    ip_key = "static_gateway"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static Gateway:", ip_key, 8)
                    self.update_field_value(ip_key, value)
                
                elif key == "staticSubnet":
                    # Handle Static Subnet
                    ip_key = "static_subnet"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static Subnet Mask:", ip_key, 9)
                    self.update_field_value(ip_key, value)
                
        # Check if we're at the end of the EEPROM values list
        elif "End of EEPROM Values" in response:
            # Store original values for comparison when saving
            self.original_values = self.eeprom_values.copy()
            self.status_label.config(text="Configuration loaded successfully")
            
            # Reset loading flag
            self.loading = False
            
            # Force the UI to update
            self.frame.update()
    def value_changed(self, key):
        """Handle when a field value changes"""
        # Get current value
        field = self.eeprom_fields[key]
        widget = field["widget"]
        field_type = field["type"]
        
        # Get the current value based on field type
        if field_type == "boolean":
            current_value = str(widget.var.get()).lower()
        else:
            current_value = widget.var.get()
        
        # Store the new value
        self.eeprom_values[key] = current_value
        
        # Check if changed from original
        if key in self.original_values:
            if current_value != self.original_values[key]:
                # Value changed - highlight in red
                if field_type == "boolean":
                    # For checkboxes, we can't easily change colors
                    pass
                else:
                    widget.config(foreground="red")
                self.modified = True
            else:
                # Value matches original - reset to default color
                if field_type != "boolean":
                    widget.config(foreground="black")
        
        # Enable/disable save button based on modifications
        if self.modified:
            self.save_btn.config(state=tk.NORMAL)
        else:
            self.save_btn.config(state=tk.DISABLED)
        
        # Update status
        self.status_label.config(text="Configuration modified - press Save to apply changes")
    
    def save_configuration(self):
        """Save modified configuration to the device"""
        if not self.app.serial_connected and not self.app.tcp_connected:
            messagebox.showerror("Error", "Not connected to a device")
            return
        
        # Collect modified values
        modified_values = {}
        for key, value in self.eeprom_values.items():
            if key in self.original_values and value != self.original_values[key]:
                modified_values[key] = value
        
        if not modified_values:
            messagebox.showinfo("Info", "No changes to save")
            return
        
        # Process each modified value and send appropriate command
        for key, value in modified_values.items():
            # Device name
            if key == "device_name":
                self.app.send_command(f"setDeviceName:{value}")
            
            # WiFi settings
            elif key.startswith("wifi") and key.endswith("_ssid"):
                slot = key[4:5]  # Extract slot number
                password_key = f"wifi{slot}_password"
                if password_key in modified_values:
                    # Only send if both SSID and password modified
                    self.app.send_command(f"connectWifi:{value}:{modified_values[password_key]}:{slot}")
            
            # Static IP settings
            elif key == "static_ip_enabled":
                if value.lower() in ("true", "yes", "1", "on"):
                    self.app.send_command("enableStaticIP")
                else:
                    self.app.send_command("disableStaticIP")
            elif key == "static_ip":
                # Send full static IP configuration if IP changed
                gateway = self.eeprom_values.get("static_gateway", "192.168.4.1")
                subnet = self.eeprom_values.get("static_subnet", "255.255.255.0")
                dns1 = self.eeprom_values.get("static_dns1", "8.8.8.8")
                dns2 = self.eeprom_values.get("static_dns2", "8.8.4.4")
                self.app.send_command(f"configureStaticIP:{value}:{gateway}:{subnet}:{dns1}:{dns2}")
            
            # Sound MQTT settings
            elif key == "sound_mqtt_url":
                port = self.eeprom_values.get("sound_mqtt_port", "8883")
                username = self.eeprom_values.get("sound_mqtt_username", "")
                if "sound_mqtt_password" in modified_values:
                    password = modified_values.get("sound_mqtt_password", "")
                    if password == "********":
                        # Password not changed, use placeholder to indicate no change
                        password = "-"
                else:
                    # Not modified, keep existing password on device
                    password = "-"
                self.app.send_command(f"configureMQTTServer:{value}:{port}:{username}:{password}")
            
            # Light MQTT settings 
            elif key == "light_mqtt_url":
                port = self.eeprom_values.get("light_mqtt_port", "8883")
                username = self.eeprom_values.get("light_mqtt_username", "")
                password = modified_values.get("light_mqtt_password", "")
                if password == "********":
                    # Password not changed, use placeholder to indicate no change
                    password = "-"
                self.status_label.config(text="Light MQTT config not supported by device")
        
        # Update status
        self.status_label.config(text="Configuration saved to device")
        messagebox.showinfo("Success", "Configuration saved to device")
        
        # Reload configuration to verify changes
        self.load_configuration()
