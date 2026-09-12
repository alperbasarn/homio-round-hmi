#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "DisplayPanelBackend.h"
#include "TouchPanel.h"
#include <cstdint>

// Forward declarations to avoid circular dependencies
class KnobController;
class SoundController;
class LightController;
class ModeController;
class InitializationScreen;
class EnvironmentInfoScreen;
class DeviceInfoScreen;
class MediaController;
enum Mode {
  INITIALIZATION,
  INFO,           // EnvironmentInfoScreen (time, weather, temperature)
  DEVICE_INFO,    // DeviceInfoScreen (WiFi, internet, MQTT, battery)
  CALIBRATE_ORIENTATION,
  SOUND,
  LIGHT,
  HOME,
  SLEEP
};

class DisplayController {
private:
  TouchPanel* touchPanel;
  KnobController* knobController;
  SoundController* soundController;
  LightController* lightController;
  ModeController* modeController;
  InitializationScreen* initializationScreen;
  EnvironmentInfoScreen* infoScreen;
  DeviceInfoScreen* deviceInfoScreen;
  MediaController* mediaController;
  DisplayPanelBackend* panelBackend;

  Mode currentMode;
  int64_t lastActivityTime;
  bool displayIsOn;
  int initializationProgress;
  int64_t initializationStartTime;
  int64_t initializationCompleteTime;

  void transitionToMode(Mode newMode, int transitionType = 0);
  void checkActivityAndAutoSwitch();
  void resetActivityTime();
  

public:
  DisplayController(TouchPanel* touch, DisplayPanelBackend* displayPanelBackend);
  void init();
  void update();
  
  void setMode(Mode mode);
  Mode getMode() const;

  void registerKnobController(KnobController* knob);
  void registerSoundController(SoundController* sound);
  void registerLightController(LightController* light);
  void registerModeController(ModeController* mode);
  void registerInitializationScreen(InitializationScreen* init);
  void registerInfoScreen(EnvironmentInfoScreen* info);
  void registerDeviceInfoScreen(DeviceInfoScreen* deviceInfo);
  void registerMediaController(MediaController* media);
  
  void incrementSetpoint();
  void decrementSetpoint();
  void setSetpoint(int setpoint);

  void turnDisplayOff();
  void turnDisplayOn();

  ~DisplayController();
};

#endif
