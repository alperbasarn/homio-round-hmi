#include "SetupHandler.h"
#include "LoopHandler.h"

void setup() {
  // Check if we're waking from deep sleep
  esp_sleep_wakeup_cause_t wakeup_reason = esp_sleep_get_wakeup_cause();
  
  // If waking from deep sleep, perform a full restart
  if (wakeup_reason == ESP_SLEEP_WAKEUP_EXT1) {
    Serial.begin(115200);
    Serial.println("Waking from deep sleep - performing system reset");
    delay(100);  // Small delay to ensure Serial output completes
    esp_restart();  // Full system restart 
    return;  // Never reached
  }
  
  // Create and use SetupHandler for initialization
  SetupHandler setupHandler;
  
  // Handle initialization until setup is complete
  bool setupComplete = false;
  while (!setupComplete) {
    setupComplete = setupHandler.handle();
  }
  
  // SetupHandler is destroyed here after setup is complete
}

void loop() {
  // Create static LoopHandler instance to manage the main loop
  static LoopHandler loopHandler;
  
  // If a sleep handler is initialized, register the LoopHandler with it
  static bool sleepHandlerRegistered = false;
  if (!sleepHandlerRegistered && sleepHandler != nullptr) {
    sleepHandler->setLoopHandler(&loopHandler);
    sleepHandlerRegistered = true;
  }
  
  // Handle the main loop
  loopHandler.handle();
}