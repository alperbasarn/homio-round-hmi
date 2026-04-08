#include "SetupHandler.h"

// Global component instances
WiFiTCPClient* tcpClient = nullptr;
Arduino_GFX* gfx = nullptr;
DisplayController* displayController = nullptr;
EEPROMManager eepromManager;
KnobController* knobController = nullptr;
CommandHandler* commandHandler = nullptr;
SleepHandler* sleepHandler = nullptr;
TaskHandle_t displayTask = nullptr;
TaskHandle_t networkTask = nullptr;
SemaphoreHandle_t xMutex = nullptr;

// Initialization flags
volatile bool displayInitialized = false;
volatile bool networkInitialized = false;
volatile bool initComplete = false;

// Constructor explicitly initializes mutex first thing
SetupHandler::SetupHandler() : 
    currentState(INIT_START), 
    progressValue(0) {
    
    Serial.begin(115200);
    Serial.println("\n\n----- Starting device initialization -----");
    
    // Check PSRAM
    if (psramFound()) {
        Serial.println("PSRAM found and initialized");
        Serial.printf("Total PSRAM: %d bytes\n", ESP.getPsramSize());
        Serial.printf("Free PSRAM: %d bytes\n", ESP.getFreePsram());
    } else {
        Serial.println("No PSRAM detected");
    }
    
    // Create synchronization primitives FIRST
    if (xMutex == nullptr) {
        xMutex = xSemaphoreCreateMutex();
        if (xMutex == nullptr) {
            Serial.println("ERROR: Failed to create mutex!");
        } else {
            Serial.println("Mutex created successfully");
        }
    }
    
    // Safeguard - make sure we have initial values for globals
    displayInitialized = false;
    networkInitialized = false;
    initComplete = false;
}

SetupHandler::~SetupHandler() {
    // Don't delete resources here, as they're used by the main application
    Serial.println("Setup complete, SetupHandler destroyed");
}

bool SetupHandler::handle() {
    bool setupComplete = false;
    
    // Always ensure display is updated first
    if (displayController) {
        // Update the display to show current progress
        displayController->update();
        delay(1); // Brief delay to allow screen refresh
    }
    
    static unsigned long lastTransitionTime = 0;
    unsigned long currentTime = millis();
    
    // Add delay between state transitions to allow screen to update
    if (currentTime - lastTransitionTime < 50) {
        return setupComplete; // Return early to give time for screen updates
    }
    
    switch (currentState) {
        case INIT_START:
            Serial.println("Starting initialization sequence");
            currentState = INIT_DISPLAY;
            lastTransitionTime = currentTime;
            break;
            
        case INIT_DISPLAY:
            initDisplay();
            displayInitialized = true;
            updateProgress(10);
            currentState = INIT_EEPROM;
            lastTransitionTime = currentTime;
            break;
            
        case INIT_EEPROM:
            initEEPROM();
            updateProgress(30);
            currentState = INIT_KNOB;
            lastTransitionTime = currentTime;
            break;
            
        case INIT_KNOB:
            initKnob();
            updateProgress(50);
            currentState = INIT_COMMAND_HANDLER;
            lastTransitionTime = currentTime;
            break;
            
        case INIT_COMMAND_HANDLER:
            initCommandHandler();
            updateProgress(70);
            currentState = INIT_SLEEP_HANDLER;
            lastTransitionTime = currentTime;
            break;
            
        case INIT_SLEEP_HANDLER:
            initSleepHandler();
            updateProgress(80);
            currentState = INIT_WIFI_TCP;
            lastTransitionTime = currentTime;
            break;
            
        case INIT_WIFI_TCP:
            // Do WiFi initialization
            initWiFiTCP();
            networkInitialized = true;
            updateProgress(90);
            currentState = START_TASKS;
            lastTransitionTime = currentTime;
            break;
            
        case START_TASKS:
      {
        static unsigned long completeStartTime = 0;
        static bool tasksStarted = false;
        
        if (!tasksStarted) {
          startTasks();
          updateProgress(100);
          completeStartTime = currentTime;
          tasksStarted = true;
          Serial.println("Tasks started, showing 100% progress");
        }
        
        // Non-blocking delay - show 100% for 200ms
        if (currentTime - completeStartTime >= 1000) {
          currentState = SETUP_COMPLETE;
          Serial.println("Setup complete!");
          setupComplete = true;
          initComplete = true;
          lastTransitionTime = currentTime;
          tasksStarted = false; // Reset for next time
        } else {
          // Keep updating display while we wait
          if (displayController) {
            displayController->update();
          }
        }
      }
      break;
      
    case SETUP_COMPLETE:
      setupComplete = true;
      break;
    }
    
    return setupComplete;
}

void SetupHandler::updateProgress(int progress) {
    progressValue = progress;
    if (displayController) {
        // Force a display update to show progress
        displayController->updateInitProgress(progressValue);
        
        // Call update immediately to refresh the screen
        displayController->update();
        
        // Small delay to allow the screen to refresh
        delay(10);
    }
}

void SetupHandler::initEEPROM() {
    Serial.println("Initializing EEPROM...");
    eepromManager.begin();
    Serial.println("EEPROM initialized");
}

void SetupHandler::initDisplay() {
    Serial.println("Initializing display...");
    
    // Important: Initialize gfx first
    if (!gfx) {
        Serial.println("Error: gfx is NULL, initializing it");
        Arduino_DataBus* bus = create_default_Arduino_DataBus();
        gfx = new Arduino_GC9A01(bus, DF_GFX_RST, 0 /* rotation */, false /* IPS */);
    }
    
    if (!gfx->begin()) {
        Serial.println("Error: gfx->begin() failed!");
    }
    
    // Then initialize display controller
    if (!displayController) {
        displayController = new DisplayController(gfx, 6, 7, 19, 5, nullptr);
        displayController->initScreen();
    }
    
    Serial.println("Display initialized");
}

void SetupHandler::initKnob() {
    Serial.println("Initializing knob controller...");
    knobController = new KnobController(KNOB_RX_PIN, KNOB_TX_PIN);
    knobController->begin(9600);
    
    // Register with display controller
    if (displayController) {
        displayController->registerKnobController(knobController);
    }
    Serial.println("Knob controller initialized");
}

void SetupHandler::initWiFiTCP() {
    Serial.println("Initializing WiFi/TCP...");
    
    // Create TCP client if not already created
    if (tcpClient == nullptr) {
        tcpClient = new WiFiTCPClient(&eepromManager);
    }
    
    // Start initialization but don't wait for MQTT connection
    tcpClient->initialize();
    
    // Update TCP client in display controller
    if (displayController) {
        displayController->setTCPClient(tcpClient);
    }
    
    // Start MQTT initialization in a separate task
    xTaskCreatePinnedToCore(
        [](void* param) {
            // Wait a moment for other operations to complete
            vTaskDelay(100 / portTICK_PERIOD_MS);
            
            Serial.println("Starting MQTT connection in background task...");
            if (tcpClient) {
                tcpClient->initializeMQTT();
                tcpClient->connectToMQTTServer();
            }
            
            vTaskDelete(NULL);
        },
        "MQTTConnectTask",
        8192,
        nullptr,
        1,
        NULL,
        NETWORK_CORE
    );
    
    Serial.println("WiFi/TCP initialized (MQTT connecting in background)");
}

void SetupHandler::initCommandHandler() {
    Serial.println("Initializing command handler...");
    commandHandler = new CommandHandler(displayController, &eepromManager, tcpClient);
    commandHandler->initialize();
    
    // Register knob controller with command handler
    if (knobController) {
        commandHandler->registerKnobController(knobController);
    }
    Serial.println("Command handler initialized");
}

void SetupHandler::initSleepHandler() {
    Serial.println("Initializing sleep handler...");
    sleepHandler = SleepHandler::getInstance(&xMutex);
    sleepHandler->setInactivityTimeout(30000);  // 30 seconds
    sleepHandler->disable();  // Disabled by default, will be enabled after tasks start
    Serial.println("Sleep handler initialized");
}

void SetupHandler::startTasks() {
    Serial.println("Setup complete, ready for LoopHandler to start tasks");
    
    // Enable sleep handler
    if (sleepHandler && displayController && commandHandler) {
        sleepHandler->registerControllers(displayController, commandHandler);
        sleepHandler->disable();  // Will be enabled by LoopHandler
    }
    
    // Small delay to let UI update before switching to INFO mode
    
    // Switch to INFO mode after initialization
    if (displayController) {
        displayController->setMode(INFO);
    }
}

void SetupHandler::printMemoryStats() {
    Serial.println("Memory Statistics:");
    Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
    if (psramFound()) {
        Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
        Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
    }
    Serial.printf("Minimum free heap: %u bytes\n", ESP.getMinFreeHeap());
}