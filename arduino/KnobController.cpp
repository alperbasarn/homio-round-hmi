#include "KnobController.h"

KnobController::KnobController(int rxPin, int txPin)
  : softSerial(rxPin, txPin), commandFromKnob(""), hasNewMessage(false) {}

void KnobController::begin(int baudRate) {
  softSerial.begin(baudRate);
}

bool KnobController::getHasNewMessage() {
  return hasNewMessage;
}

void KnobController::askSetpoint() {
  sendCommand("sp?");
}

void KnobController::update() {
  hasNewMessage = false;  
  while (softSerial.available()) {
    hasNewMessage = true;
    commandFromKnob = "";
    lastActionTime = millis();

    while (softSerial.available()) {
      char c = softSerial.read();
      if (c == '\n') break;  // End of command
      commandFromKnob += c;
      delay(3);  // Prevents serial buffer overflow
    }
    
    // No longer process commands directly
    // Commands will be processed by CommandHandler
  }
}

String KnobController::getReceivedCommand() {
  return commandFromKnob;
}

void KnobController::sendCommand(String command) {
  softSerial.print(command);
  //Serial.println("Sent command to knob:" + command);
}