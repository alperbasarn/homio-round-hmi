#ifndef SLEEP_HANDLER_H
#define SLEEP_HANDLER_H

#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "DisplayController.h"
#include "CommandHandler.h"
// Remove LoopHandler.h include

// Add forward declaration instead
class LoopHandler;

// GPIO pins for wakeup
#define WAKEUP_PIN_TOUCH GPIO_NUM_5   // Wakeup when touch screen is pressed and interrupt pin pulls down
#define WAKEUP_PIN_KNOB GPIO_NUM_16   // Wakeup when motor controller sends a serial command and RX (Pin 16) goes LOW

// Timeout values
#define POWER_SAVE_TIMEOUT 30000      // 30 seconds of inactivity for power save mode
#define DEEP_SLEEP_TIMEOUT 3600000    // 1 hour of inactivity for deep sleep

class SleepHandler {
private:
    DisplayController* displayController;
    CommandHandler* commandHandler;
    LoopHandler* loopHandler;         // Added LoopHandler reference
    
    unsigned long lastActivityTime;
    unsigned long inactivityTimeout;  // Timeout in milliseconds
    bool enabled;                     // Flag to enable/disable sleep functionality
    bool powerSaveEnabled;            // Flag for power save mode
    
    SemaphoreHandle_t* mutexPtr;     // Pointer to the application mutex
    
    // Configure wakeup pins for deep sleep
    void configureWakeupPins();
    
    // Private constructor - enforces singleton pattern
    SleepHandler(SemaphoreHandle_t* mutex);

public:
    // Creates the singleton instance of SleepHandler
    static SleepHandler* getInstance(SemaphoreHandle_t* mutex = nullptr);
    
    // Sets the controllers used to check for activity
    void registerControllers(DisplayController* display, CommandHandler* command);
    
    // Sets the LoopHandler for power management
    void setLoopHandler(LoopHandler* loop);
    
    // Sets the inactivity timeout in milliseconds
    void setInactivityTimeout(unsigned long timeoutMs);
    
    // Reset the activity timer (called when activity is detected)
    void resetActivityTime();
    
    // Enable sleep functionality
    void enable();
    
    // Disable sleep functionality
    void disable();
    
    // Check if sleep handler is enabled
    bool isEnabled();
    
    // Check for inactivity and handle sleep if needed
    void checkActivity();
    
    // Immediately enter deep sleep mode
    void enterDeepSleep();
    
    // Destructor
    ~SleepHandler();
};

#endif // SLEEP_HANDLER_H