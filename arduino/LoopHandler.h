#ifndef LOOP_HANDLER_H
#define LOOP_HANDLER_H

#include <Arduino.h>
#include "DisplayController.h"
#include "TouchPanel.h"
#include "WiFiTCPClient.h"
#include "EEPROMManager.h"
#include "CommandHandler.h"
#include "KnobController.h"
// Remove SleepHandler.h include
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// Add forward declaration instead
class SleepHandler;

// Forward declarations - actual instances declared in SetupHandler.cpp
extern WiFiTCPClient* tcpClient;
extern DisplayController* displayController;
extern EEPROMManager eepromManager;
extern KnobController* knobController;
extern CommandHandler* commandHandler;
extern SleepHandler* sleepHandler;
extern SemaphoreHandle_t xMutex;
extern TaskHandle_t displayTask;
extern TaskHandle_t networkTask;

class LoopHandler {
public:
    LoopHandler();
    ~LoopHandler();
    
    void handle(); // Main loop handler
    void setPowerSaveMode(bool enabled); // Set power save mode

private:
    unsigned long lastMemCheckTime;
    bool tasksStarted;
    bool powerSaveMode;
    
    void checkMemory();
    void startTasks();
};

#endif // LOOP_HANDLER_H