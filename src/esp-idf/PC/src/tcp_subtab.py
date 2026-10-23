# -*- coding: utf-8 -*-
"""
Created on Fri Mar 28 03:13:44 2025

@author: Alper Basaran
"""

import tkinter as tk
from tkinter import ttk
import socket
import threading
import time
from terminal_classes import TCPTerminal

class TCPSubTab:
    def __init__(self, notebook, app):
        self.app = app
        self.frame = ttk.Frame(notebook)
        self.tcp_socket = None
        self.reader_thread = None
        self.running = False
        self.server_scan_thread = None
        self.found_servers = []
        
        # Create UI elements
        self.create_ui()
        
        # Initial server scan
        self.scan_for_servers()
        
    def create_ui(self):
        # Top frame for connection controls
        controls_frame = ttk.Frame(self.frame)
        controls_frame.pack(fill=tk.X, padx=5, pady=5)
        
        # Server selection
        ttk.Label(controls_frame, text="Server:").grid(row=0, column=0, padx=5, pady=5, sticky=tk.W)
        self.server_combo = ttk.Combobox(controls_frame, width=30)
        self.server_combo.grid(row=0, column=1, padx=5, pady=5, sticky=tk.W)
        
        # Port
        ttk.Label(controls_frame, text="Port:").grid(row=0, column=2, padx=5, pady=5, sticky=tk.W)
        self.port_entry = ttk.Entry(controls_frame, width=10)
        self.port_entry.grid(row=0, column=3, padx=5, pady=5, sticky=tk.W)
        self.port_entry.insert(0, "23")  # Default telnet port
        
        # Scan, Connect, Disconnect buttons
        self.scan_btn = ttk.Button(controls_frame, text="Scan", command=self.scan_for_servers)
        self.scan_btn.grid(row=0, column=4, padx=5, pady=5)
        
        self.connect_btn = ttk.Button(controls_frame, text="Connect", command=self.connect_tcp)
        self.connect_btn.grid(row=0, column=5, padx=5, pady=5)
        
        self.disconnect_btn = ttk.Button(controls_frame, text="Disconnect", command=self.disconnect_tcp, state=tk.DISABLED)
        self.disconnect_btn.grid(row=0, column=6, padx=5, pady=5)
        
        # Create terminal interface using TCPTerminal
        self.terminal = TCPTerminal(self.frame, self.app)
        
        # Scan status
        self.scan_status = ttk.Label(self.frame, text="")
        self.scan_status.pack(fill=tk.X, padx=5, pady=2, anchor=tk.W)
    
    def scan_for_servers(self):
        """Scan the network for TCP servers"""
        if self.server_scan_thread and self.server_scan_thread.is_alive():
            self.terminal.log_message("Server scan already in progress", "status")
            return
            
        # Clear previous results
        self.found_servers = []
        self.server_combo['values'] = []
        
        # Update status
        self.scan_status.config(text="Scanning for servers...")
        self.scan_btn.config(state=tk.DISABLED)
        
        # Start scan in background thread
        self.server_scan_thread = threading.Thread(target=self._scan_network, daemon=True)
        self.server_scan_thread.start()
    
    def _scan_network(self):
        """Background thread to scan network for TCP servers"""
        try:
            # Get local IP address
            s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            s.connect(("8.8.8.8", 80))
            local_ip = s.getsockname()[0]
            s.close()
            
            # Parse the network prefix
            ip_parts = local_ip.split('.')
            network_prefix = '.'.join(ip_parts[0:3]) + '.'
            
            # Try the IP range starting from .1
            for i in range(1, 255):
                if i % 10 == 0:
                    # Update scan status every 10 hosts
                    self.app.root.after(0, lambda: self.scan_status.config(text=f"Scanning: {network_prefix}{i}/255..."))
                
                target_ip = network_prefix + str(i)
                target_port = int(self.port_entry.get())
                
                try:
                    # Try to connect with a short timeout
                    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                    s.settimeout(0.1)
                    result = s.connect_ex((target_ip, target_port))
                    s.close()
                    
                    if result == 0:  # Port is open
                        self.found_servers.append(f"{target_ip}:{target_port}")
                        # Update UI from the main thread
                        self.app.root.after(0, self.update_server_list)
                
                except:
                    # Ignore connection errors
                    pass
            
            # Show final results
            self.app.root.after(0, lambda: self.scan_status.config(text=f"Scan complete: found {len(self.found_servers)} servers"))
            self.app.root.after(0, lambda: self.scan_btn.config(state=tk.NORMAL))
            
            if self.found_servers:
                self.terminal.log_message(f"Found {len(self.found_servers)} TCP servers", "status")
            else:
                self.terminal.log_message("No TCP servers found", "status")
        
        except Exception as e:
            self.app.root.after(0, lambda: self.scan_status.config(text=f"Scan error: {str(e)}"))
            self.app.root.after(0, lambda: self.scan_btn.config(state=tk.NORMAL))
            self.terminal.log_message(f"Server scan error: {str(e)}", "error")
    
    def update_server_list(self):
        """Update the server dropdown with found servers"""
        self.server_combo['values'] = self.found_servers
        if self.found_servers:
            self.server_combo.current(0)
    
    def connect_tcp(self):
        """Connect to the selected TCP server"""
        if not self.server_combo.get() and not self.port_entry.get():
            self.terminal.log_message("No server/port specified", "error")
            return
            
        try:
            # Parse server and port
            if ":" in self.server_combo.get():
                server, port = self.server_combo.get().split(":")
                port = int(port)
            else:
                server = self.server_combo.get()
                port = int(self.port_entry.get())
            
            # Create socket and connect
            self.tcp_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self.tcp_socket.settimeout(3.0)  # 3 second connection timeout
            self.tcp_socket.connect((server, port))
            self.tcp_socket.settimeout(0.1)  # Short timeout for reads
            
            # Update UI
            self.connect_btn.config(state=tk.DISABLED)
            self.disconnect_btn.config(state=tk.NORMAL)
            self.terminal.send_btn.config(state=tk.NORMAL)
            
            # Update app's connection status
            self.app.tcp_connected = True
            self.app.connected_device = self.tcp_socket
            self.app.connection_type = "tcp"
            self.app.update_connection_status()
            
            # Start reader thread
            self.running = True
            self.reader_thread = threading.Thread(target=self.read_tcp, daemon=True)
            self.reader_thread.start()
            
            self.terminal.log_message(f"Connected to TCP server at {server}:{port}", "status")
        
        except Exception as e:
            self.terminal.log_message(f"TCP connection error: {str(e)}", "error")
    
    def disconnect_tcp(self):
        """Disconnect from the TCP server"""
        if self.tcp_socket:
            self.running = False
            if self.reader_thread:
                self.reader_thread.join(timeout=1.0)
            
            # Close the socket
            try:
                self.tcp_socket.close()
            except:
                pass
            self.tcp_socket = None
            
            # Update UI
            self.connect_btn.config(state=tk.NORMAL)
            self.disconnect_btn.config(state=tk.DISABLED)
            self.terminal.send_btn.config(state=tk.DISABLED)
            
            # Update app's connection status
            self.app.tcp_connected = False
            self.app.connected_device = None
            self.app.connection_type = None
            self.app.update_connection_status()
            
            self.terminal.log_message("TCP disconnected", "status")
    
    def read_tcp(self):
        """Background thread to read from TCP socket"""
        buffer = ""
        
        while self.running and self.tcp_socket:
            try:
                # Read data if available
                data = self.tcp_socket.recv(1024).decode('utf-8')
                if data:
                    buffer += data
                    
                    # Process complete lines
                    while '\n' in buffer:
                        line, buffer = buffer.split('\n', 1)
                        line = line.strip()
                        if line:
                            # Process the received line
                            self.terminal.log_message(line, "tcp_received")
                            try:
                                self.app.process_received_command(line)
                            except Exception as e:
                                self.terminal.log_message(f"Parse error: {e}", "error")
                elif not data:  # Connection closed by server
                    self.terminal.log_message("Connection closed by server", "status")
                    self.running = False
                    self.app.root.after(100, self.disconnect_tcp)
                    break
                
                # Short delay to prevent CPU overload
                time.sleep(0.05)
                
            except socket.timeout:
                # Just a timeout, continue
                pass
            except Exception as e:
                self.terminal.log_message(f"TCP read error: {str(e)}", "error")
                # On error, break the loop and disconnect
                self.running = False
                self.app.root.after(100, self.disconnect_tcp)
                break
