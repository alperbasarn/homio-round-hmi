#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "TouchPanel.h"
#include <string>
#include <atomic>
#include <cstdint>

// Forward declarations
class KnobController;
class SoundController;
class LightController;
class ModeController;
class InitializationScreen;
class EnvironmentInfoScreen;
class DeviceInfoScreen;
class PlaceholderPageScreen;
class MediaController;

enum Mode {
  INITIALIZATION,
  INFO,
  DEVICE_INFO,
  MATRIX_PAGE,
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
  PlaceholderPageScreen* placeholderScreen;
  MediaController* mediaController;

  Mode currentMode;
  int64_t lastActivityTime;
  int64_t initializationStartTime;
  bool displayIsOn;
  int initializationProgress;
  int64_t initializationCompleteTime;
  int currentPageX;
  int currentPageY;
  std::atomic<int> pendingModeRequest;
  std::atomic<int> pendingPageRequest;

  void transitionToMode(Mode newMode, int transitionType = 0);
  void checkActivityAndAutoSwitch();
  void resetActivityTime();
  bool handleMatrixNavigationGesture(touch_gesture_t gesture);
  bool navigateToMatrixPage(int x, int y);
  void resetActiveScreen();

public:
  DisplayController(TouchPanel* touch);
  void init();
  void update();

  void setMode(Mode mode);
  Mode getMode() const;
  bool showNamedScreen(const std::string& screenName);
  std::string getModeName() const;

  void registerKnobController(KnobController* knob);
  void registerSoundController(SoundController* sound);
  void registerLightController(LightController* light);
  void registerModeController(ModeController* mode);
  void registerInitializationScreen(InitializationScreen* init);
  void registerInfoScreen(EnvironmentInfoScreen* info);
  void registerDeviceInfoScreen(DeviceInfoScreen* deviceInfo);
  void registerPlaceholderScreen(PlaceholderPageScreen* placeholder);
  void registerMediaController(MediaController* media);

  void incrementSetpoint();
  void decrementSetpoint();
  void setSetpoint(int setpoint);

  void turnDisplayOff();
  void turnDisplayOn();

  ~DisplayController();
};

#endif
