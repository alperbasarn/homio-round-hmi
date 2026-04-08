#include <Arduino_GFX_Library.h>
#include "DisplayController.h"
#include "TouchPanel.h"
#include "WiFiTCPClient.h"
#include "EEPROMManager.h"
#include "CommandHandler.h"
#include "KnobController.h"
#include "SleepHandler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Knob communication pins
#define KNOB_RX_PIN 16
#define KNOB_TX_PIN 15

// Core definitions
#define NETWORK_CORE 1  // Core 1 for network/TCP operations (higher priority)
#define DISPLAY_CORE 0  // Core 0 for display and other operations (lower priority)

// Task priorities
#define NETWORK_TASK_PRIORITY 2  // Higher priority for network operations
#define DISPLAY_TASK_PRIORITY 1  // Lower priority for display operations
#define INIT_TASK_PRIORITY 3     // Highest priority for initialization

// Task stack sizes
#define NETWORK_STACK_SIZE 8192
#define DISPLAY_STACK_SIZE 8192

// Global variables
WiFiTCPClient* tcpClient = NULL;
Arduino_DataBus* bus = create_default_Arduino_DataBus();
Arduino_GFX* gfx = new Arduino_GC9A01(bus, DF_GFX_RST, 0 /* rotation */, false /* IPS */);
DisplayController* displayController = NULL;
EEPROMManager eepromManager;
KnobController* knobController = NULL;
CommandHandler* commandHandler = NULL;
SleepHandler* sleepHandler = NULL;

// Task handles
TaskHandle_t displayTask = NULL;
TaskHandle_t networkTask = NULL;
TaskHandle_t initDisplayTask = NULL;
TaskHandle_t initNetworkTask = NULL;

// Mutex and semaphores for synchronizing access to shared resources
SemaphoreHandle_t xMutex = NULL;
SemaphoreHandle_t xEEPROMSemaphore = NULL;     // Signal EEPROM is initialized
SemaphoreHandle_t xTCPClientSemaphore = NULL;  // Signal TCP client is created

// Initialization flags
volatile bool displayInitialized = false;
volatile bool networkInitialized = false;
volatile bool initComplete = false;
volatile bool setupComplete = false;

// Function to print memory statistics
void printMemoryStats() {
  Serial.println("Memory Statistics:");
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  if (psramFound()) {
    Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
  }
  Serial.printf("Minimum free heap: %u bytes\n", ESP.getMinFreeHeap());
}