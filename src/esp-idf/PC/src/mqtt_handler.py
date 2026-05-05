import paho.mqtt.client as mqtt
import ssl
import time
import threading
import queue

class MQTTHandler:
    """Handles MQTT connection and message processing"""
    
    def __init__(self, app, message_callback=None):
        # Message queue for thread-safe communication
        self.message_queue = queue.Queue()
        
        # Initialize client
        self.create_new_client()
        
        self.connected = False
        self.message_callback = message_callback
        self.app = app
        
        # Default MQTT broker settings
        self.broker_address = "4b664591e7764720a1ae5483e793dfa0.s1.eu.hivemq.cloud"
        self.broker_port = 8883
        self.username = "test123"
        self.password = "Test1234"
        
        # Client ID to identify messages from this client
        self.client_id = "PC"
        
        # Start queue processing on the main thread
        if self.app and hasattr(self.app, 'root'):
            self.start_queue_processing()
    
    def start_queue_processing(self):
        """Start processing messages from the queue on the main thread"""
        self.process_queue()
    
    def process_queue(self):
        """Process messages from the queue (runs on main thread)"""
        try:
            # Process all messages currently in the queue
            while not self.message_queue.empty():
                message_type, message, is_self = self.message_queue.get_nowait()
                
                # Now safely call the callback from the main thread
                if self.message_callback:
                    self.message_callback(message, message_type, is_self=is_self)
                
                # Mark the task as done
                self.message_queue.task_done()
        except queue.Empty:
            pass  # Queue is empty, that's fine
        except Exception as e:
            print(f"Error processing message queue: {e}")
        
        # Schedule the next queue check
        if self.app and hasattr(self.app, 'root'):
            self.app.root.after(100, self.process_queue)
    
    def create_new_client(self):
        """Create a new MQTT client instance"""
        # Use the recommended client creation approach
        self.client = mqtt.Client()
        
        # Set callbacks
        self.client.on_connect = self.on_connect
        self.client.on_message = self.on_message
        self.client.on_disconnect = self.on_disconnect
        self.client.on_publish = self.on_publish
    
    def set_broker(self, address, port=8883, username="", password=""):
        """Set MQTT broker parameters"""
        self.broker_address = address
        self.broker_port = port
        self.username = username
        self.password = password
    
    def connect(self):
        """Connect to MQTT broker"""
        try:
            # Create a new client instance to avoid SSL configuration issues
            self.create_new_client()
            
            if self.username and self.password:
                self.client.username_pw_set(self.username, self.password)
            
            # Enable TLS/SSL for secure connection
            self.client.tls_set(cert_reqs=ssl.CERT_REQUIRED, tls_version=ssl.PROTOCOL_TLS)
            self.client.tls_insecure_set(False)
            
            # Connect with timeout
            self.client.connect(self.broker_address, self.broker_port, 60)
            self.client.loop_start()
            
            # Wait for connection to establish or timeout
            start_time = time.time()
            while not self.connected and (time.time() - start_time) < 5:
                time.sleep(0.1)
            
            if not self.connected:
                # Add error message to queue
                self.message_queue.put(("error", "Connection timeout, could not connect to broker", False))
                return False
                
            return True
        except Exception as e:
            # Add error message to queue
            self.message_queue.put(("error", f"Connection error: {str(e)}", False))
            return False
    
    def disconnect(self):
        """Disconnect from MQTT broker"""
        try:
            self.client.loop_stop()
            self.client.disconnect()
            self.connected = False
        except Exception as e:
            # Add error message to queue
            self.message_queue.put(("error", f"Disconnect error: {str(e)}", False))
    
    def on_connect(self, client, userdata, flags, rc):
        """Callback when connected to MQTT broker"""
        if rc == 0:
            self.connected = True
            
            # Add status message to queue
            self.message_queue.put(("status", "Connected to MQTT broker", False))
            self.message_queue.put(("status", "Self-message filtering disabled", False))
            
            # Subscribe to ESP32 topics
            self.client.subscribe("esp32/sound/control")
            self.client.subscribe("esp32/sound/setpoint")
            self.client.subscribe("esp32/sound/response")
            self.client.subscribe("esp32/sound/get_state")
            self.client.subscribe("esp32/command/response")
            
            # Update UI status (handled in the main thread via queue)
            if self.app and hasattr(self.app, 'sound_tab'):
                broker_info = f"{self.broker_address}:{self.broker_port}"
                self.message_queue.put(("mqtt_connected", broker_info, False))
        else:
            self.connected = False
            
            error_messages = {
                1: "Connection refused - incorrect protocol version",
                2: "Connection refused - invalid client identifier",
                3: "Connection refused - server unavailable",
                4: "Connection refused - bad username or password",
                5: "Connection refused - not authorized"
            }
            error_msg = error_messages.get(rc, f"Unknown error code: {rc}")
            
            # Add error message to queue
            self.message_queue.put(("error", f"Failed to connect: {error_msg}", False))
            
            # Update UI status (handled in the main thread via queue)
            if self.app and hasattr(self.app, 'sound_tab'):
                self.message_queue.put(("mqtt_disconnected", "", False))
    
    def on_disconnect(self, client, userdata, rc):
        """Callback when disconnected from MQTT broker"""
        self.connected = False
        
        reason = "Unexpected disconnection" if rc != 0 else "Requested disconnection"
        # Add status message to queue
        self.message_queue.put(("status", f"Disconnected from MQTT broker: {reason}", False))
        
        # Update UI status (handled in the main thread via queue)
        if self.app and hasattr(self.app, 'sound_tab'):
            self.message_queue.put(("mqtt_disconnected", "", False))
    
    def on_message(self, client, userdata, msg):
        """Callback when message is received"""
        topic = msg.topic
        try:
            payload = msg.payload.decode()
        except UnicodeDecodeError:
            payload = f"Binary data ({len(msg.payload)} bytes)"
        
        # Detect if message is from ourselves
        is_self_message = f"sender={self.client_id}" in payload
        
        # Add received message to queue
        self.message_queue.put(("mqtt_received", f"Received [{topic}]: {payload}", is_self_message))
        
        # Handle message processing (through the queue)
        if not is_self_message and topic == "esp32/sound/control":
            # Extract command
            command = payload
            if "," in payload:
                parts = payload.split(",")
                for part in parts:
                    if not part.startswith("sender="):
                        command = part.strip()
                        break
            
            self.message_queue.put(("mqtt_control", command, False))
                
        elif not is_self_message and topic == "esp32/sound/setpoint" and "setpoint:" in payload:
            try:
                setpoint_str = payload.split("setpoint:")[1].split(",")[0].strip()
                setpoint = int(setpoint_str)
                self.message_queue.put(("mqtt_setpoint", str(setpoint), False))
            except:
                pass
                
        elif topic == "esp32/sound/get_state" and "request" in payload:
            self.message_queue.put(("mqtt_get_state", "", False))
        elif topic == "esp32/command/response":
            self.message_queue.put(("mqtt_command_response", payload, False))
    
    def on_publish(self, client, userdata, mid):
        """Callback when message is published successfully"""
        pass
    
    def publish(self, topic, message):
        """Publish message to topic with sender ID"""
        if self.connected:
            # Add sender ID to message if not already included
            if not message.startswith(f"sender={self.client_id}"):
                full_message = f"sender={self.client_id},{message}"
            else:
                full_message = message
                
            result = self.client.publish(topic, full_message, qos=1)
            if result.rc == mqtt.MQTT_ERR_SUCCESS:
                # Add sent message to queue
                self.message_queue.put(("mqtt_sent", f"Sent [{topic}]: {message}", False))
                return True
            else:
                error_msg = mqtt.error_string(result.rc)
                # Add error message to queue
                self.message_queue.put(("error", f"Failed to publish message: {error_msg}", False))
                return False
        else:
            # Add error message to queue
            self.message_queue.put(("error", "Not connected to MQTT broker", False))
            return False
    
    def is_connected(self):
        """Check if connected to MQTT broker"""
        return self.connected
        
    def get_broker_info(self):
        """Get the broker address and port"""
        if self.broker_address:
            return f"{self.broker_address}:{self.broker_port}"
        return ""
