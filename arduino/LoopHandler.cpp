#include "LoopHandler.h"
#include "SleepHandler.h" 

// Core definitions from SetupHandler.h
#define NETWORK_CORE 1
#define DISPLAY_CORE 0
#define NETWORK_TASK_PRIORITY 1
#define DISPLAY_TASK_PRIORITY 2
#define NETWORK_STACK_SIZE 8192
#define DISPLAY_STACK_SIZE 8192

// Update displayTask_func and networkTask_func to support power save mode
void displayTask_func(void* parameter) {
  Serial.println("[Display Task] Starting on core " + String(xPortGetCoreID()));


  // Track if we're in power save mode
  bool localPowerSaveMode = false;

  for (;;) {
    // Get power save mode state via task priority
    UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
    localPowerSaveMode = (currentPriority <= tskIDLE_PRIORITY + 1);

    // Check if display controller exists before using it
    if (displayController != nullptr) {
      // Take mutex when accessing shared resources
      if (xSemaphoreTake(xMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // Null check again inside critical section
        if (displayController != nullptr) {
          displayController->update();
        }

        // Only update knobController if it exists
        if (knobController != nullptr) {
          knobController->update();
        }

        xSemaphoreGive(xMutex);
      }
    }

    // Adjust delay based on power save mode
    if (localPowerSaveMode) {
      // Longer delay in power save mode - 100ms
      vTaskDelay(300 / portTICK_PERIOD_MS);
    } else {
      // Normal delay - 10ms
      vTaskDelay(5 / portTICK_PERIOD_MS);
    }
  }
}

void networkTask_func(void* parameter) {
  Serial.println("[Network Task] Starting on core " + String(xPortGetCoreID()));

  // Small delay to ensure everything is initialized
  //vTaskDelay(100 / portTICK_PERIOD_MS);

  unsigned long lastMemCheckTime = 0;
  bool localPowerSaveMode = false;

  for (;;) {
    // Get power save mode state via task priority
    UBaseType_t currentPriority = uxTaskPriorityGet(NULL);
    localPowerSaveMode = (currentPriority <= tskIDLE_PRIORITY + 1);

    // Check if critical components are initialized
    if (tcpClient != nullptr && commandHandler != nullptr) {
      // Take mutex when accessing shared resources
      if (xSemaphoreTake(xMutex, 10 / portTICK_PERIOD_MS) == pdTRUE) {
        // Null check again inside critical section
        if (tcpClient != nullptr) {
          tcpClient->update();
        }
        if (commandHandler != nullptr) {
          commandHandler->update();
        }

        // Print memory stats every 30 seconds (or 60 in power save mode)
        unsigned long currentMillis = millis();
        unsigned long memInterval = localPowerSaveMode ? 60000 : 30000;

        if (currentMillis - lastMemCheckTime > memInterval) {
          // Print memory stats
          Serial.println("Memory Statistics:");
          Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
          if (psramFound()) {
            Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
            Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
          }
          Serial.printf("Minimum free heap: %u bytes\n", ESP.getMinFreeHeap());

          lastMemCheckTime = currentMillis;
        }

        xSemaphoreGive(xMutex);
      }

      // Check for sleep if sleep handler is initialized
      if (sleepHandler != nullptr) {
        sleepHandler->checkActivity();
      }
    }

    // Adjust delay based on power save mode
    if (localPowerSaveMode) {
      // Longer delay in power save mode - 250ms
      vTaskDelay(250 / portTICK_PERIOD_MS);
    } else {
      // Normal delay - 10ms
      vTaskDelay(10 / portTICK_PERIOD_MS);
    }
  }
}

LoopHandler::LoopHandler()
  : lastMemCheckTime(0), tasksStarted(false), powerSaveMode(false) {
  Serial.println("LoopHandler initialized");
}

LoopHandler::~LoopHandler() {
  // Nothing to clean up
}

void LoopHandler::handle() {
  // Start tasks if not already started
  if (!tasksStarted) {
    startTasks();
    tasksStarted = true;
  }

  // Periodically check memory usage
  unsigned long currentMillis = millis();
  if (currentMillis - lastMemCheckTime > 30000) {  // Every 30 seconds
    checkMemory();
    lastMemCheckTime = currentMillis;
  }

  // Yield to FreeRTOS tasks
  vTaskDelay(1);
}

void LoopHandler::startTasks() {
  Serial.println("Starting core tasks from LoopHandler...");

  // Create main display task
  xTaskCreatePinnedToCore(
    displayTask_func,       // Task function
    "DisplayTask",          // Name
    DISPLAY_STACK_SIZE,     // Stack size
    nullptr,                // Parameter
    DISPLAY_TASK_PRIORITY,  // Priority
    &displayTask,           // Task handle
    DISPLAY_CORE            // Core
  );

  // Create main network task
  xTaskCreatePinnedToCore(
    networkTask_func,       // Task function
    "NetworkTask",          // Name
    NETWORK_STACK_SIZE,     // Stack size
    nullptr,                // Parameter
    NETWORK_TASK_PRIORITY,  // Priority
    &networkTask,           // Task handle
    NETWORK_CORE            // Core
  );

  // Small delay to let tasks start
  delay(100);

  // Enable sleep handler now that all components are initialized
  if (sleepHandler && displayController && commandHandler) {
    sleepHandler->enable();
    Serial.println("Sleep handler enabled by LoopHandler");
  }

  Serial.println("Core tasks started by LoopHandler");
}

void LoopHandler::checkMemory() {
  Serial.println("Memory Statistics:");
  Serial.printf("Free heap: %u bytes\n", ESP.getFreeHeap());
  if (psramFound()) {
    Serial.printf("Total PSRAM: %u bytes\n", ESP.getPsramSize());
    Serial.printf("Free PSRAM: %u bytes\n", ESP.getFreePsram());
  }
  Serial.printf("Minimum free heap: %u bytes\n", ESP.getMinFreeHeap());
}

void LoopHandler::setPowerSaveMode(bool enabled) {
  if (powerSaveMode != enabled) {
    powerSaveMode = enabled;

    // Update task intervals based on power save mode
    extern TaskHandle_t displayTask;
    extern TaskHandle_t networkTask;

    if (displayTask != nullptr) {

      // Set lower priority and reduced frequency for display task in power save mode
      if (powerSaveMode) {
        displayController->turnDisplayOff();
        Serial.println("Setting display task to power save mode");
        vTaskPrioritySet(displayTask, tskIDLE_PRIORITY + 1);
        // No need to adjust frequency as we'll do this in the task itself
      } else {
        Serial.println("Setting display task to normal mode");
        vTaskPrioritySet(displayTask, DISPLAY_TASK_PRIORITY);
        displayController->turnDisplayOn();
      }
    }

    if (networkTask != nullptr) {
      // Set lower priority and reduced frequency for network task in power save mode
      if (powerSaveMode) {
        Serial.println("Setting network task to power save mode");
        vTaskPrioritySet(networkTask, tskIDLE_PRIORITY + 1);
        // No need to adjust frequency as we'll do this in the task itself
      } else {
        Serial.println("Setting network task to normal mode");
        vTaskPrioritySet(networkTask, NETWORK_TASK_PRIORITY);
      }
    }

    Serial.println(powerSaveMode ? "Power save mode enabled" : "Power save mode disabled");
  }
}