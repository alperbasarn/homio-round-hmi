# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 03:13:44 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk
import serial.tools.list_ports
import serial
import threading
import time
from terminal_classes import SerialTerminal

class SerialSubTab:
    def __init__(self, notebook, app):
        self.app = app
        self.frame = ttk.Frame(notebook)
        self.serial_port = None
        self.reader_thread = None
        self.running = False
        
        # Create UI elements
        self.create_ui()
        
        # Initial port scan
        self.refresh_ports()
        
    def create_ui(self):
        # Top frame for connection controls
        controls_frame = ttk.Frame(self.frame)
        controls_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Port selection
        ttk.Label(controls_frame, text="Port:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.port_combo = ttk.Combobox(controls_frame, width=15)
        self.port_combo.grid(row=0, column=1, padx=5, pady=5, sticky=tk.W)
        
        # Baud rate
        ttk.Label(controls_frame, text="Baud:").grid(row=0, column=2, padx=5, pady=5, sticky=tk.W)
        self.baud_combo = ttk.Combobox(controls_frame, width=10, values=["9600", "115200"])
        self.baud_combo.grid(row=0, column=3, padx=5, pady=5, sticky=tk.W)
        self.baud_combo.current(0)  # Default to 9600
        
        # Refresh, Connect, Disconnect buttons
        self.refresh_btn = ttk.Button(controls_frame, text="Refresh", command=self.refresh_ports)
        self.refresh_btn.grid(row=0, column=4, padx=5, pady=5)
        
        self.connect_btn = ttk.Button(controls_frame, text="Connect", command=self.connect_serial)
        self.connect_btn.grid(row=0, column=5, padx=5, pady=5)
        
        self.disconnect_btn = ttk.Button(controls_frame, text="Disconnect", command=self.disconnect_serial, state=tk.DISABLED)
        self.disconnect_btn.grid(row=0, column=6, padx=5, pady=5)
        
        # Create terminal interface using SerialTerminal class
        self.terminal = SerialTerminal(self.frame, self.app)
    
    def refresh_ports(self):
        """Scan for available serial ports"""
        ports = serial.tools.list_ports.comports()
        port_names = [port.device for port in ports]
        self.port_combo['values'] = port_names
        
        if port_names:
            self.port_combo.current(0)
            self.terminal.log_message(f"Found {len(port_names)} serial ports", "status")
        else:
            self.terminal.log_message("No serial ports found", "status")
    
    def connect_serial(self):
        """Connect to the selected serial port"""
        if not self.port_combo.get():
            self.terminal.log_message("No port selected", "error")
            return
            
        try:
            port = self.port_combo.get()
            baud = int(self.baud_combo.get())
            
            self.serial_port = serial.Serial(port, baud, timeout=0.1)
            
            # Query device name with timeout
            self.terminal.log_message("Querying device name...", "status")
            self.serial_port.write("getDeviceName\n".encode())
            
            # Wait for response with 1 second timeout
            device_name = None
            start_time = time.time()
            while time.time() - start_time < 1.0:
                if self.serial_port.in_waiting:
                    # Read raw bytes
                    raw_data = self.serial_port.readline()
                    
                    # Try to decode with error handling
                    try:
                        response = raw_data.decode('utf-8', errors='replace').strip()
                    except UnicodeDecodeError:
                        # If still failing, try to decode as latin-1 (which accepts any byte)
                        response = raw_data.decode('latin-1').strip()
                    
                    if response:
                        # Save the first non-empty response as device name
                        # Ignore error messages that start with [
                        if not response.startswith("["):
                            device_name = response
                            break
                time.sleep(0.05)
            
            # Check if we got a device name
            if not device_name:
                self.terminal.log_message("No response from device, connection failed", "error")
                self.serial_port.close()
                return
                
            # Update UI
            self.connect_btn.config(state=tk.DISABLED)
            self.disconnect_btn.config(state=tk.NORMAL)
            self.terminal.send_btn.config(state=tk.NORMAL)
            
            # Update app's connection status with device name and port
            self.app.serial_connected = True
            self.app.connected_device = self.serial_port
            self.app.connection_type = "serial"
            self.app.device_name = device_name
            self.app.port_name = port
            self.app.update_connection_status()
            
            # Start reader thread
            self.running = True
            self.reader_thread = threading.Thread(target=self.read_serial, daemon=True)
            self.reader_thread.start()
            
            self.terminal.log_message(f"Connected to {device_name} on {port} at {baud} baud", "status")
        
        except Exception as e:
            self.terminal.log_message(f"Serial connection error: {str(e)}", "error")
    def disconnect_serial(self):
        """Disconnect from the serial port"""
        if self.serial_port and self.serial_port.is_open:
            self.running = False
            if self.reader_thread:
                self.reader_thread.join(timeout=1.0)
            
            # Close the port
            self.serial_port.close()
            self.serial_port = None
            
            # Update UI
            self.connect_btn.config(state=tk.NORMAL)
            self.disconnect_btn.config(state=tk.DISABLED)
            self.terminal.send_btn.config(state=tk.DISABLED)
            
            # Update app's connection status
            self.app.serial_connected = False
            self.app.connected_device = None
            self.app.connection_type = None
            self.app.update_connection_status()
            
            self.terminal.log_message("Serial disconnected", "status")
    
    def read_serial(self):
        """Background thread to read from serial port"""
        while self.running and self.serial_port and self.serial_port.is_open:
            try:
                # Read data if available
                if self.serial_port.in_waiting:
                    # Read raw bytes
                    raw_data = self.serial_port.readline()
                    
                    # Try to decode with error handling
                    try:
                        data = raw_data.decode('utf-8', errors='replace').strip()
                    except UnicodeDecodeError:
                        # If still failing, try to decode as latin-1 (which accepts any byte)
                        data = raw_data.decode('latin-1').strip()
                
                    if data:
                        # Process the received data
                        self.terminal.log_message(data, "serial_received")
                        try:
                            self.app.process_received_command(data)
                        except Exception as e:
                            self.terminal.log_message(f"Parse error: {e}", "error")
                
                # Short delay to prevent CPU overload
                time.sleep(0.05)
                
            except Exception as e:
                self.terminal.log_message(f"Serial read error: {str(e)}", "error")
                # On error, break the loop and disconnect
                self.running = False
                self.app.root.after(100, self.disconnect_serial)
                break
