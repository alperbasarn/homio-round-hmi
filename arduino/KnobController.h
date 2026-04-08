#ifndef KNOB_CONTROLLER_H
#define KNOB_CONTROLLER_H

#include <SoftwareSerial.h>

class KnobController {
public:
  KnobController(int rxPin, int txPin);
  void begin(int baudRate);
  void update();
  void askSetpoint();
  String getReceivedCommand();
  bool getHasNewMessage();
  void sendCommand(String command);

private:
  SoftwareSerial softSerial;
  String commandFromKnob;
  unsigned long lastActionTime;
  bool hasNewMessage;
};

#endif  // KNOB_CONTROLLER_H