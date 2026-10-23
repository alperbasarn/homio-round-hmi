# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 02:31:59 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk, messagebox

from ota_helper import OTAHelper, DEFAULT_OTA_PORT


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
        self.loading = False

        self.ota_helper = OTAHelper()

        # Create UI elements
        self.create_ui()
        self.load_ota_settings()

    def create_ui(self):
        # Top frame for load/save buttons
        button_frame = ttk.Frame(self.frame)
        button_frame.pack(fill=tk.X, padx=5, pady=5)

        self.load_btn = ttk.Button(button_frame, text="Load from Device", command=self.load_configuration)
        self.load_btn.pack(side=tk.LEFT, padx=5, pady=5)

        self.save_btn = ttk.Button(
            button_frame,
            text="Save to Device",
            command=self.save_configuration,
            state=tk.DISABLED,
        )
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
            lambda e: self.canvas.configure(scrollregion=self.canvas.bbox("all")),
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
        self.ota_frame = self.create_section_frame("OTA Update", 4)
        self.create_ota_ui()

        # Add a stretch frame at the bottom to push everything up
        stretch_frame = ttk.Frame(self.scrollable_frame)
        stretch_frame.grid(row=5, column=0, sticky="nsew", pady=20)
        self.scrollable_frame.grid_rowconfigure(5, weight=1)

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
            field.var = var
        elif value_type == "dropdown" and options:
            var = tk.StringVar(value=options[0] if options else "")
            field = ttk.Combobox(parent, values=options, textvariable=var)
            field.var = var
            field.state(["readonly"])
        else:
            var = tk.StringVar(value="")
            field = ttk.Entry(parent, textvariable=var)
            field.var = var

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

        self.eeprom_fields[key] = {"widget": field, "type": value_type}
        return field

    def create_ota_ui(self):
        self.ota_file_var = tk.StringVar(value="")
        self.ota_host_var = tk.StringVar(value="")
        self.ota_port_var = tk.StringVar(value=str(DEFAULT_OTA_PORT))
        self.ota_version_var = tk.StringVar(value="Unknown")
        self.ota_partition_var = tk.StringVar(value="Unknown")
        self.ota_runtime_status_var = tk.StringVar(value="No device data yet")
        self.ota_server_status_var = tk.StringVar(value="Server stopped")
        self.ota_action_var = tk.StringVar(value="Select a generated OTA binary to install")

        ttk.Label(self.ota_frame, text="OTA Folder:").grid(row=0, column=0, padx=5, pady=2, sticky=tk.W)
        ttk.Label(self.ota_frame, text=str(self.ota_helper.ota_dir), wraplength=540).grid(
            row=0, column=1, columnspan=3, padx=5, pady=2, sticky=tk.W
        )

        ttk.Label(self.ota_frame, text="Firmware File:").grid(row=1, column=0, padx=5, pady=2, sticky=tk.W)
        self.ota_file_combo = ttk.Combobox(self.ota_frame, textvariable=self.ota_file_var, state="readonly", width=48)
        self.ota_file_combo.grid(row=1, column=1, columnspan=3, padx=5, pady=2, sticky=tk.EW)

        ttk.Label(self.ota_frame, text="Server Host/IP:").grid(row=2, column=0, padx=5, pady=2, sticky=tk.W)
        ttk.Entry(self.ota_frame, textvariable=self.ota_host_var).grid(row=2, column=1, padx=5, pady=2, sticky=tk.EW)
        ttk.Label(self.ota_frame, text="Port:").grid(row=2, column=2, padx=5, pady=2, sticky=tk.W)
        ttk.Entry(self.ota_frame, textvariable=self.ota_port_var, width=10).grid(row=2, column=3, padx=5, pady=2, sticky=tk.W)

        button_frame = ttk.Frame(self.ota_frame)
        button_frame.grid(row=3, column=0, columnspan=4, padx=5, pady=8, sticky=tk.W)
        ttk.Button(button_frame, text="Refresh OTA Files", command=self.refresh_ota_files).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Query OTA Info", command=self.query_ota_info).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Install Selected OTA", command=self.install_selected_ota).pack(side=tk.LEFT, padx=5)
        ttk.Button(button_frame, text="Stop OTA Server", command=self.stop_ota_server).pack(side=tk.LEFT, padx=5)

        ttk.Label(self.ota_frame, text="Running Version:").grid(row=4, column=0, padx=5, pady=2, sticky=tk.W)
        ttk.Label(self.ota_frame, textvariable=self.ota_version_var).grid(row=4, column=1, padx=5, pady=2, sticky=tk.W)
        ttk.Label(self.ota_frame, text="Running Slot:").grid(row=4, column=2, padx=5, pady=2, sticky=tk.W)
        ttk.Label(self.ota_frame, textvariable=self.ota_partition_var).grid(row=4, column=3, padx=5, pady=2, sticky=tk.W)

        ttk.Label(self.ota_frame, text="Device OTA Status:").grid(row=5, column=0, padx=5, pady=2, sticky=tk.W)
        ttk.Label(self.ota_frame, textvariable=self.ota_runtime_status_var, wraplength=540).grid(
            row=5, column=1, columnspan=3, padx=5, pady=2, sticky=tk.W
        )

        ttk.Label(self.ota_frame, text="HTTP Server:").grid(row=6, column=0, padx=5, pady=2, sticky=tk.W)
        ttk.Label(self.ota_frame, textvariable=self.ota_server_status_var, wraplength=540).grid(
            row=6, column=1, columnspan=3, padx=5, pady=2, sticky=tk.W
        )

        ttk.Label(self.ota_frame, text="Last Action:").grid(row=7, column=0, padx=5, pady=2, sticky=tk.W)
        ttk.Label(self.ota_frame, textvariable=self.ota_action_var, wraplength=540).grid(
            row=7, column=1, columnspan=3, padx=5, pady=2, sticky=tk.W
        )

        self.ota_frame.grid_columnconfigure(1, weight=1)
        self.ota_frame.grid_columnconfigure(3, weight=1)

    def load_ota_settings(self):
        ota_config = {}
        if hasattr(self.app, "config_handler"):
            ota_config = self.app.config_handler.get_ota_config()

        host = ota_config.get("host", "").strip()
        if not host:
            host = self.ota_helper.auto_detect_host()

        port = ota_config.get("port", DEFAULT_OTA_PORT)
        self.ota_host_var.set(host)
        self.ota_port_var.set(str(port))
        self.refresh_ota_files()

    def save_ota_settings(self):
        if not hasattr(self.app, "config_handler"):
            return

        try:
            port = int(self.ota_port_var.get().strip())
        except ValueError:
            return

        self.app.config_handler.save_ota_config(self.ota_host_var.get().strip(), port)

    def refresh_ota_files(self):
        current_selection = self.ota_file_var.get().strip()
        ota_files = self.ota_helper.list_firmware_images()
        self.ota_file_combo["values"] = ota_files

        if current_selection in ota_files:
            self.ota_file_var.set(current_selection)
        elif ota_files:
            self.ota_file_var.set(ota_files[0])
        else:
            self.ota_file_var.set("")

        if ota_files:
            self.ota_action_var.set(f"Found {len(ota_files)} OTA image(s) in {self.ota_helper.ota_dir}")
        else:
            self.ota_action_var.set(f"No OTA images found in {self.ota_helper.ota_dir}")

    def query_ota_info(self):
        """Request OTA status from the device"""
        if not self.app.serial_connected and not self.app.tcp_connected:
            messagebox.showerror("Error", "Not connected to a device")
            return

        self.app.send_command("otaInfo")
        self.app.send_command("otaStatus")
        self.ota_action_var.set("Requested OTA info and status from device")

    def install_selected_ota(self):
        """Serve the selected OTA image and trigger device download"""
        if not self.app.serial_connected and not self.app.tcp_connected:
            messagebox.showerror("Error", "Not connected to a device")
            return

        firmware_name = self.ota_file_var.get().strip()
        if not firmware_name:
            messagebox.showerror("Error", "No OTA firmware selected")
            return

        host = self.ota_host_var.get().strip()
        if not host:
            host = self.ota_helper.auto_detect_host()
            self.ota_host_var.set(host)

        try:
            port = int(self.ota_port_var.get().strip())
        except ValueError:
            messagebox.showerror("Error", "OTA port must be a valid integer")
            return

        try:
            update_url = self.ota_helper.build_update_url(firmware_name, host, port)
        except Exception as exc:
            messagebox.showerror("OTA Error", str(exc))
            return

        if not self.app.send_command(f"otaUpdate:{update_url}"):
            messagebox.showerror("OTA Error", "Failed to send otaUpdate command to the device")
            return

        self.save_ota_settings()
        self.ota_server_status_var.set(f"Serving {self.ota_helper.ota_dir} at http://{host}:{port}/")
        self.ota_action_var.set(f"Sent OTA update command for {firmware_name}")

    def stop_ota_server(self):
        self.ota_helper.stop_server()
        self.ota_server_status_var.set("Server stopped")
        self.ota_action_var.set("Stopped local OTA HTTP server")

    def shutdown(self):
        self.ota_helper.stop_server()

    def parse_ota_fields(self, payload):
        fields = {}
        for item in payload.split(","):
            if "=" not in item:
                continue
            key, value = item.split("=", 1)
            fields[key.strip()] = value.strip()
        return fields

    def handle_ota_response(self, response):
        if response.startswith("otaInfo:"):
            payload = response.split(":", 1)[1]
            fields = self.parse_ota_fields(payload)
            self.ota_version_var.set(fields.get("version", "Unknown"))
            self.ota_partition_var.set(fields.get("partition", "Unknown"))
            self.ota_action_var.set("Updated OTA firmware information from device")
            return True

        if response.startswith("otaStatus:"):
            payload = response.split(":", 1)[1]
            fields = self.parse_ota_fields(payload)
            busy = fields.get("busy", "unknown")
            last_error = fields.get("last_error", "unknown")
            self.ota_runtime_status_var.set(f"busy={busy}, last_error={last_error}")
            return True

        if response.startswith("otaUpdate:"):
            self.ota_action_var.set(response)
            return True

        return False

    def load_configuration(self):
        """Request configuration from the device"""
        if not self.app.serial_connected and not self.app.tcp_connected:
            messagebox.showerror("Error", "Not connected to a device")
            return

        if self.loading:
            return

        self.loading = True
        self.clear_fields()
        self.app.send_command("listNVSValues")
        self.status_label.config(text="Requested configuration from device")

    def clear_fields(self):
        """Clear all configuration fields"""
        for widget in self.wifi_frame.winfo_children():
            widget.destroy()
        for widget in self.mqtt_frame.winfo_children():
            widget.destroy()
        for widget in self.device_frame.winfo_children():
            widget.destroy()
        for widget in self.misc_frame.winfo_children():
            widget.destroy()

        self.eeprom_values = {}
        self.original_values = {}
        self.eeprom_fields = {}
        self.modified = False
        self.save_btn.config(state=tk.DISABLED)

    def populate_test_data(self):
        """Populate with test data for UI development"""
        self.create_field(self.wifi_frame, "WiFi SSID (Slot 0):", "wifi0_ssid", 0)
        self.update_field_value("wifi0_ssid", "MyNetwork")

        self.create_field(self.wifi_frame, "WiFi Password (Slot 0):", "wifi0_password", 1)
        self.update_field_value("wifi0_password", "password123")

        self.create_field(self.mqtt_frame, "Sound MQTT Server:", "sound_mqtt_server", 0)
        self.update_field_value("sound_mqtt_server", "mqtt.example.com")

        self.create_field(self.mqtt_frame, "Sound MQTT Port:", "sound_mqtt_port", 1)
        self.update_field_value("sound_mqtt_port", "8883")

        self.create_field(self.device_frame, "Device Name:", "device_name", 0)
        self.update_field_value("device_name", "MyQNOB")

        self.original_values = self.eeprom_values.copy()

    def update_field_value(self, key, value):
        """Update a field's value"""
        if key in self.eeprom_fields:
            field = self.eeprom_fields[key]
            widget = field["widget"]
            field_type = field["type"]

            if field_type == "boolean":
                bool_value = value.lower() in ("true", "yes", "1", "on")
                widget.var.set(bool_value)
            elif field_type == "dropdown":
                if value in widget["values"]:
                    widget.set(value)
                else:
                    widget.current(0)
            else:
                widget.var.set(value)

            self.eeprom_values[key] = value

    def process_command_response(self, response):
        """Process a configuration response from the device"""
        if self.handle_ota_response(response):
            return

        if "=" in response:
            key_value = response.split("=", 1)
            if len(key_value) == 2:
                key = key_value[0].strip()
                value = key_value[1].strip()

                if key.startswith("wifi") and key.endswith("_ssid"):
                    slot = key[4:5]
                    ssid_key = f"wifi{slot}_ssid"
                    pass_key = f"wifi{slot}_password"

                    if ssid_key not in self.eeprom_fields:
                        self.create_field(self.wifi_frame, f"WiFi SSID (Slot {slot}):", ssid_key, int(slot) * 2)
                    if pass_key not in self.eeprom_fields:
                        self.create_field(self.wifi_frame, f"WiFi Password (Slot {slot}):", pass_key, int(slot) * 2 + 1)

                    self.update_field_value(ssid_key, value)

                elif key.startswith("wifi") and key.endswith("_password"):
                    self.update_field_value(key, "********")

                elif key == "lastConnectedNetwork":
                    slot_key = "last_wifi_slot"

                    if slot_key not in self.eeprom_fields:
                        self.create_field(
                            self.wifi_frame,
                            "Last Used WiFi Slot:",
                            slot_key,
                            6,
                            value_type="dropdown",
                            options=["0", "1", "2"],
                        )

                    self.update_field_value(slot_key, value)

                elif key == "deviceName":
                    device_key = "device_name"
                    if device_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Device Name:", device_key, 0)
                    self.update_field_value(device_key, value)

                elif key.startswith("sound") and key.endswith("URL"):
                    mqtt_key = "sound_mqtt_url"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT URL:", mqtt_key, 0)
                    self.update_field_value(mqtt_key, value)

                elif key.startswith("sound") and key.endswith("Port"):
                    mqtt_key = "sound_mqtt_port"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT Port:", mqtt_key, 1)
                    self.update_field_value(mqtt_key, value)

                elif key.startswith("sound") and key.endswith("Username"):
                    mqtt_key = "sound_mqtt_username"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT Username:", mqtt_key, 2)
                    self.update_field_value(mqtt_key, value)

                elif key.startswith("sound") and key.endswith("Password"):
                    mqtt_key = "sound_mqtt_password"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Sound MQTT Password:", mqtt_key, 3)
                    self.update_field_value(mqtt_key, "********")

                elif key.startswith("light") and key.endswith("URL"):
                    mqtt_key = "light_mqtt_url"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT URL:", mqtt_key, 4)
                    self.update_field_value(mqtt_key, value)

                elif key.startswith("light") and key.endswith("Port"):
                    mqtt_key = "light_mqtt_port"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT Port:", mqtt_key, 5)
                    self.update_field_value(mqtt_key, value)

                elif key.startswith("light") and key.endswith("Username"):
                    mqtt_key = "light_mqtt_username"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT Username:", mqtt_key, 6)
                    self.update_field_value(mqtt_key, value)

                elif key.startswith("light") and key.endswith("Password"):
                    mqtt_key = "light_mqtt_password"
                    if mqtt_key not in self.eeprom_fields:
                        self.create_field(self.mqtt_frame, "Light MQTT Password:", mqtt_key, 7)
                    self.update_field_value(mqtt_key, "********")

                elif key == "staticIPEnabled":
                    ip_key = "static_ip_enabled"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static IP Enabled:", ip_key, 6, value_type="boolean")
                    self.update_field_value(ip_key, value)

                elif key == "staticIP":
                    ip_key = "static_ip"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static IP:", ip_key, 7)
                    self.update_field_value(ip_key, value)

                elif key == "staticGateway":
                    ip_key = "static_gateway"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static Gateway:", ip_key, 8)
                    self.update_field_value(ip_key, value)

                elif key == "staticSubnet":
                    ip_key = "static_subnet"
                    if ip_key not in self.eeprom_fields:
                        self.create_field(self.device_frame, "Static Subnet Mask:", ip_key, 9)
                    self.update_field_value(ip_key, value)

        elif "End of EEPROM Values" in response:
            self.original_values = self.eeprom_values.copy()
            self.status_label.config(text="Configuration loaded successfully")
            self.loading = False
            self.frame.update()

    def value_changed(self, key):
        """Handle when a field value changes"""
        field = self.eeprom_fields[key]
        widget = field["widget"]
        field_type = field["type"]

        if field_type == "boolean":
            current_value = str(widget.var.get()).lower()
        else:
            current_value = widget.var.get()

        self.eeprom_values[key] = current_value

        if key in self.original_values:
            if current_value != self.original_values[key]:
                if field_type != "boolean":
                    widget.config(foreground="red")
                self.modified = True
            else:
                if field_type != "boolean":
                    widget.config(foreground="black")

        if self.modified:
            self.save_btn.config(state=tk.NORMAL)
        else:
            self.save_btn.config(state=tk.DISABLED)

        self.status_label.config(text="Configuration modified - press Save to apply changes")

    def save_configuration(self):
        """Save modified configuration to the device"""
        if not self.app.serial_connected and not self.app.tcp_connected:
            messagebox.showerror("Error", "Not connected to a device")
            return

        modified_values = {}
        for key, value in self.eeprom_values.items():
            if key in self.original_values and value != self.original_values[key]:
                modified_values[key] = value

        if not modified_values:
            messagebox.showinfo("Info", "No changes to save")
            return

        for key, value in modified_values.items():
            if key == "device_name":
                self.app.send_command(f"setDeviceName:{value}")

            elif key.startswith("wifi") and key.endswith("_ssid"):
                slot = key[4:5]
                password_key = f"wifi{slot}_password"
                if password_key in modified_values:
                    self.app.send_command(f"connectWifi:{value}:{modified_values[password_key]}:{slot}")

            elif key == "static_ip_enabled":
                if value.lower() in ("true", "yes", "1", "on"):
                    self.app.send_command("enableStaticIP")
                else:
                    self.app.send_command("disableStaticIP")
            elif key == "static_ip":
                gateway = self.eeprom_values.get("static_gateway", "192.168.4.1")
                subnet = self.eeprom_values.get("static_subnet", "255.255.255.0")
                dns1 = self.eeprom_values.get("static_dns1", "8.8.8.8")
                dns2 = self.eeprom_values.get("static_dns2", "8.8.4.4")
                self.app.send_command(f"configureStaticIP:{value}:{gateway}:{subnet}:{dns1}:{dns2}")

            elif key == "sound_mqtt_url":
                port = self.eeprom_values.get("sound_mqtt_port", "8883")
                username = self.eeprom_values.get("sound_mqtt_username", "")
                if "sound_mqtt_password" in modified_values:
                    password = modified_values.get("sound_mqtt_password", "")
                    if password == "********":
                        password = "-"
                else:
                    password = "-"
                self.app.send_command(f"configureMQTTServer:{value}:{port}:{username}:{password}")

            elif key == "light_mqtt_url":
                self.status_label.config(text="Light MQTT config not supported by device")

        self.status_label.config(text="Configuration saved to device")
        messagebox.showinfo("Success", "Configuration saved to device")
        self.load_configuration()
