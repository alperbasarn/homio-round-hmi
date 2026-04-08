import tkinter as tk
from tkinter import scrolledtext, ttk, messagebox
import time
import re

# Import components
from windows_audio_controller import WindowsAudioController
from media_controller import MediaController
from mqtt_handler import MQTTHandler
from connect_tab import ConnectTab
from configure_tab import ConfigureTab
from sound_controller_tab import SoundControllerTab
from config_handler import ConfigHandler  # Import the new config handler

# Define a unique sender ID for this application
SENDER_ID = "QNOB"

class QNOBApp:
    """Main application class for QNOB Control System"""
    
    def __init__(self, root):
        self.device_name = "Unknown"
        self.port_name = ""
        self.root = root
        self.root.title("QNOB Control System")
        self.root.geometry("900x650")
        self.root.minsize(800, 600)
        
        # Initialize config handler
        self.config_handler = ConfigHandler()
        
        # Global connection status variables
        self.serial_connected = False
        self.tcp_connected = False
        self.connected_device = None  # Will store either the serial or tcp connection object
        self.connection_type = None   # Will be either "serial" or "tcp"
        
        # Initialize controller components
        self.audio_controller = WindowsAudioController(self.handle_volume_change)
        # Set root for thread safety
        self.audio_controller.set_root(self.root)
        
        self.media_controller = MediaController()
        
        # Client ID to identify this application
        self.client_id = SENDER_ID
        
        # Create notebook with tabs
        self.notebook = ttk.Notebook(root)
        self.notebook.pack(fill=tk.BOTH, expand=True, padx=5, pady=5)
        
        # Set up status bar at bottom of window
        self.create_status_bar()
        
        # Initialize MQTT handler with config 
        mqtt_config = self.config_handler.get_mqtt_config()
        self.mqtt_handler = MQTTHandler(self, self.handle_message)
        self.mqtt_handler.set_broker(
            mqtt_config.get("broker", ""),
            mqtt_config.get("port", 8883),
            mqtt_config.get("username", ""),
            mqtt_config.get("password", "")
        )
        
        # Sound Controller Tab (Main tab)
        self.sound_tab = SoundControllerTab(self.notebook, self)
        self.notebook.add(self.sound_tab.frame, text="Sound & Media")
        
        # Connect Tab (with serial and TCP subtabs)
        self.connect_tab = ConnectTab(self.notebook, self)
        self.notebook.add(self.connect_tab.frame, text="Connect")
        
        # Configure Tab (initially disabled)
        self.configure_tab = ConfigureTab(self.notebook, self)
        self.notebook.add(self.configure_tab.frame, text="Configure")
        
        # Initially disable Configure tab until connection is established
        self.notebook.tab(2, state="disabled")
        
        # Bind tab change event to handle tab switching
        self.notebook.bind("<<NotebookTabChanged>>", self.on_tab_changed)
        
        # Start monitoring volume changes
        self.audio_controller.start_monitoring()
        
        # Update status
        self.update_status("Ready")
    
    def create_status_bar(self):
        """Create a status bar at the bottom of the window"""
        self.status_frame = ttk.Frame(self.root, relief=tk.SUNKEN, padding=(2, 2))
        self.status_frame.pack(side=tk.BOTTOM, fill=tk.X)
        
        self.connection_label = ttk.Label(self.status_frame, text="Not Connected", foreground="red")
        self.connection_label.pack(side=tk.LEFT, padx=5)
        
        # Separator
        ttk.Separator(self.status_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=2)
        
        self.volume_label = ttk.Label(self.status_frame, text="Volume: 0%")
        self.volume_label.pack(side=tk.LEFT, padx=5)
        
        # Separator
        ttk.Separator(self.status_frame, orient=tk.VERTICAL).pack(side=tk.LEFT, fill=tk.Y, padx=5, pady=2)
        
        self.status_label = ttk.Label(self.status_frame, text="Ready")
        self.status_label.pack(side=tk.LEFT, padx=5, fill=tk.X, expand=True)
    
    def update_connection_status(self):
        """Update the connection status in the status bar"""
        if self.serial_connected:
            self.connection_label.config(text=f"Connected to {self.device_name} on {self.port_name}", foreground="green")
            self.notebook.tab(2, state="normal")  # Enable Configure tab
        elif self.tcp_connected:
            self.connection_label.config(text="Connected via TCP", foreground="green")
            self.notebook.tab(2, state="normal")  # Enable Configure tab
        else:
            self.connection_label.config(text="Not Connected", foreground="red")
            self.notebook.tab(2, state="disabled")  # Disable Configure tab
            
    def update_status(self, message):
        """Update the status message in the status bar"""
        self.status_label.config(text=message)

    def send_command(self, command):
        """Send a command over the active serial or TCP connection"""
        if not command:
            return False

        if not (self.serial_connected or self.tcp_connected):
            self.update_status("Not connected")
            if hasattr(self, 'connect_tab'):
                self.connect_tab.log_message("Cannot send command: Not connected", "error")
            return False

        payload = command.strip() + "\n"
        try:
            if self.serial_connected and self.connected_device:
                self.connected_device.write(payload.encode())
            elif self.tcp_connected and self.connected_device:
                self.connected_device.sendall(payload.encode())
            else:
                return False

            if hasattr(self, 'connect_tab'):
                self.connect_tab.log_message(command, "sent")
            return True
        except Exception as e:
            if hasattr(self, 'connect_tab'):
                self.connect_tab.log_message(f"Send failed: {e}", "error")
            return False
        
    def handle_volume_change(self, volume):
        """Handle volume change from Windows audio controller
        This is called on the main thread via root.after()"""
        try:
            self.volume_label.config(text=f"Volume: {volume}%")
            
            # Update volume in sound tab
            if hasattr(self, 'sound_tab'):
                self.sound_tab.update_volume(volume)
            
            # If connected, send volume to device
            if self.serial_connected or self.tcp_connected:
                self.send_command(f"setpoint={volume}")
        except Exception as e:
            print(f"Error updating volume UI: {e}")
            
    def handle_message(self, message, message_type="status", is_self=False):
        """Central message handler for all tabs"""
        try:
            # Special message types for MQTT status updates
            if message_type == "mqtt_connected":
                if hasattr(self, 'sound_tab'):
                    self.sound_tab.update_mqtt_status(True, message)
                return
            elif message_type == "mqtt_disconnected":
                if hasattr(self, 'sound_tab'):
                    self.sound_tab.update_mqtt_status(False)
                return
            elif message_type == "mqtt_control":
                # Process media command
                if message == "play":
                    if hasattr(self, 'media_controller'):
                        is_playing = self.media_controller.play_pause()
                        if hasattr(self, 'sound_tab'):
                            self.sound_tab.update_playing_state(is_playing)
                        if hasattr(self, 'mqtt_handler'):
                            state = "playing" if is_playing else "paused"
                            self.mqtt_handler.publish("esp32/sound/response", state)
                elif message == "pause":
                    if hasattr(self, 'media_controller'):
                        is_playing = self.media_controller.play_pause()
                        if hasattr(self, 'sound_tab'):
                            self.sound_tab.update_playing_state(is_playing)
                        if hasattr(self, 'mqtt_handler'):
                            state = "playing" if is_playing else "paused"
                            self.mqtt_handler.publish("esp32/sound/response", state)
                elif message == "forward":
                    if hasattr(self, 'media_controller'):
                        self.media_controller.next_track()
                elif message == "rewind":
                    if hasattr(self, 'media_controller'):
                        self.media_controller.prev_track()
                return
            elif message_type == "mqtt_setpoint":
                # Process setpoint
                try:
                    setpoint = int(message)
                    if hasattr(self, 'audio_controller'):
                        self.audio_controller.set_volume_percent(setpoint)
                    if hasattr(self, 'sound_tab'):
                        self.sound_tab.update_volume(setpoint, send_to_device=False)
                except:
                    pass
                return
            elif message_type == "mqtt_get_state":
                # Send state
                if hasattr(self, 'audio_controller') and hasattr(self, 'media_controller'):
                    volume = self.audio_controller.get_volume_percent()
                    is_playing = self.media_controller.get_playing_state()
                    state = "playing" if is_playing else "paused"
                    if hasattr(self, 'mqtt_handler'):
                        self.mqtt_handler.publish("esp32/sound/response", f"state:{state},volume:{volume}")
                return
            elif message_type == "mqtt_command_response":
                self.process_received_command(message)
                return
                
            # Regular message handling
            if hasattr(self, 'connect_tab'):
                self.connect_tab.log_message(message, message_type, is_self)
            
            # Update status bar
            if message_type == "status":
                self.update_status(message)
                
            # For received commands, process them
            if message_type == "received":
                self.process_received_command(message)
        except Exception as e:
            print(f"Error in handle_message: {e}")
            
    def process_received_command(self, message):
        """Process commands received from the device"""
        # Extract the actual message content
        if isinstance(message, str):
            # Message might be formatted as "Received [topic]: payload"
            match = re.search(r"Received.*?: (.+)$", message)
            if match:
                command = match.group(1)
            else:
                command = message
        else:
            command = str(message)
            
        # Process setpoint updates
        if command.startswith("setpoint="):
            try:
                setpoint = int(command.split("=")[1])
                # Update UI but don't send back to device
                self.sound_tab.update_volume(setpoint, send_to_device=False)
            except ValueError:
                pass
                
        # Process playing state updates
        elif command == "playing" or command == "paused":
            is_playing = command == "playing"
            self.sound_tab.update_playing_state(is_playing)
            
        # Pass information to Configure tab for NVS values
        elif not command.startswith("sender=") and self.notebook.index(self.notebook.select()) == 2:
            if "=" in command or "End of EEPROM Values" in command:
                if hasattr(self.configure_tab, 'process_command_response'):
                    self.configure_tab.process_command_response(command)

    def on_tab_changed(self, event):
        """Handle tab change events"""
        selected_tab = self.notebook.index(self.notebook.select())
        
        # If switching to Configure tab, load EEPROM values
        if selected_tab == 2 and (self.serial_connected or self.tcp_connected):
            self.configure_tab.load_configuration()
            
    def on_closing(self):
        """Clean up resources when closing the app"""
        try:
            # Stop audio monitoring
            if hasattr(self, 'audio_controller'):
                self.audio_controller.stop_monitoring()
                
            # Close connections
            if self.serial_connected and self.connected_device:
                self.connected_device.close()
                
            if self.tcp_connected and self.connected_device:
                self.connected_device.close()
        except Exception as e:
            print(f"Error cleaning up: {e}")
            
        # Destroy the window
        self.root.destroy()
