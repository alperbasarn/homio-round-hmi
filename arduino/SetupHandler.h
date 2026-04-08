#ifndef SETUP_HANDLER_H
#define SETUP_HANDLER_H

#include <Arduino.h>
#include "DisplayController.h"
#include "TouchPanel.h"
#include "WiFiTCPClient.h"
#include "EEPROMManager.h"
#include "CommandHandler.h"
#include "KnobController.h"
#include "SleepHandler.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Core definitions
#define NETWORK_CORE 1  // Core 1 for network/TCP operations (higher priority)
#define DISPLAY_CORE 0  // Core 0 for display and other operations (lower priority)

// Task priorities
#define NETWORK_TASK_PRIORITY 1  // Higher priority for network operations
#define DISPLAY_TASK_PRIORITY 2  // Lower priority for display operations
#define INIT_TASK_PRIORITY 3     // Highest priority for initialization

// Task stack sizes
#define NETWORK_STACK_SIZE 8192
#define DISPLAY_STACK_SIZE 8192

// Knob communication pins
#define KNOB_RX_PIN 16
#define KNOB_TX_PIN 15

// Forward declaration for global components
extern WiFiTCPClient* tcpClient;
extern Arduino_GFX* gfx;
extern DisplayController* displayController;
extern EEPROMManager eepromManager;
extern KnobController* knobController;
extern CommandHandler* commandHandler;
extern SleepHandler* sleepHandler;
extern TaskHandle_t displayTask;
extern TaskHandle_t networkTask;
extern SemaphoreHandle_t xMutex;

// Additional flags to track initialization state
extern volatile bool displayInitialized;
extern volatile bool networkInitialized;
extern volatile bool initComplete;

class SetupHandler {
public:
    enum SetupState {
        INIT_START,
        INIT_EEPROM,
        INIT_DISPLAY,
        INIT_KNOB,
        INIT_WIFI_TCP,
        INIT_COMMAND_HANDLER,
        INIT_SLEEP_HANDLER,
        START_TASKS,
        SETUP_COMPLETE
    };

    SetupHandler();
    ~SetupHandler();

    bool handle(); // Main handle method for state machine, returns true when complete

public:  // Changed from private so lambda can access initWiFiTCP
    // State machine
    SetupState currentState;
    int progressValue;
    
    // Initialize specific components
    void initEEPROM();
    void initDisplay();
    void initKnob();
    void initWiFiTCP();
    void initCommandHandler();
    void initSleepHandler();
    void startTasks();
    
    // Helper methods
    void updateProgress(int progress);
    void printMemoryStats();
    bool displayInitialized;
};



#endif // SETUP_HANDLER_H