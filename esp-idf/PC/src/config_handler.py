import json
import os

class ConfigHandler:
    """Handles reading and writing application configuration"""
    
    def __init__(self, config_file="qnob_config.json"):
        self.config_file = config_file
        self.config = self.load_config()
    
    def load_config(self):
        """Load configuration from file or create default if not exists"""
        if os.path.exists(self.config_file):
            try:
                with open(self.config_file, 'r') as f:
                    return json.load(f)
            except Exception as e:
                print(f"Error loading config: {e}")
                return self.get_default_config()
        else:
            # Create default config
            config = self.get_default_config()
            self.save_config(config)
            return config
    
    def save_config(self, config=None):
        """Save configuration to file"""
        if config is None:
            config = self.config
        
        try:
            with open(self.config_file, 'w') as f:
                json.dump(config, f, indent=4)
            return True
        except Exception as e:
            print(f"Error saving config: {e}")
            return False
    
    def get_default_config(self):
        """Get default configuration settings"""
        return {
            "mqtt": {
                "broker": "4b664591e7764720a1ae5483e793dfa0.s1.eu.hivemq.cloud",
                "port": 8883,
                "username": "test123",
                "password": "Test1234"
            }
        }
    
    def get_mqtt_config(self):
        """Get MQTT configuration settings"""
        return self.config.get("mqtt", self.get_default_config()["mqtt"])
    
    def save_mqtt_config(self, broker, port, username, password):
        """Save MQTT configuration settings"""
        if "mqtt" not in self.config:
            self.config["mqtt"] = {}
        
        self.config["mqtt"]["broker"] = broker
        self.config["mqtt"]["port"] = port
        self.config["mqtt"]["username"] = username
        self.config["mqtt"]["password"] = password
        
        return self.save_config()