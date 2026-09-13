#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "input/InputEvent.h"
#include "model/MutationQueue.h"
#include <string>
#include <atomic>
#include <cstdint>

// Forward declarations
class SoundController;
class LightController;
class ModeController;
class InitializationScreen;
class EnvironmentInfoScreen;
class DeviceInfoScreen;
class PlaceholderPageScreen;

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
  SoundController* soundController;
  LightController* lightController;
  ModeController* modeController;
  InitializationScreen* initializationScreen;
  EnvironmentInfoScreen* infoScreen;
  DeviceInfoScreen* deviceInfoScreen;
  PlaceholderPageScreen* placeholderScreen;

  Mode currentMode;
  int64_t lastActivityTime;
  int64_t initializationStartTime;
  bool displayIsOn;
  int brightnessPercent;
  int initializationProgress;
  int64_t initializationCompleteTime;
  int currentPageX;
  int currentPageY;
  uint32_t lastActivityPostMs;
  std::atomic<int> pendingModeRequest;
  std::atomic<int> pendingPageRequest;
  // WP1.4 shadow phase: every knob token and navigation change is also posted as a record
  // (INPT-05, MDL-31). Nothing in this class calls a model setter.
  model::MutationQueue* modelQueue;

  void transitionToMode(Mode newMode, int transitionType = 0);
  void checkActivityAndAutoSwitch();
  void resetActivityTime();
  bool handleMatrixNavigationGesture(input::Gesture gesture);
  void applyKnobDelta(int16_t delta);
  bool navigateToMatrixPage(int x, int y);
  void resetActiveScreen();
  void postNavigationScreen(Mode mode);
  void postNavigation(int field, int32_t value);

public:
  DisplayController();
  // INPT-14: the router delivers every touch and knob event here; nothing in this class polls.
  void onInput(const input::InputEvent& event);
  // Any router activity, delivered or consumed (a wake tap): resets the inactivity clock and
  // leaves SLEEP.
  void noteActivity();
  void init();
  void update();

  void setMode(Mode mode);
  Mode getMode() const;
  bool showNamedScreen(const std::string& screenName);
  std::string getModeName() const;

  void registerSoundController(SoundController* sound);
  void registerLightController(LightController* light);
  void registerModeController(ModeController* mode);
  void registerInitializationScreen(InitializationScreen* init);
  void registerInfoScreen(EnvironmentInfoScreen* info);
  void registerDeviceInfoScreen(DeviceInfoScreen* deviceInfo);
  void registerPlaceholderScreen(PlaceholderPageScreen* placeholder);
  void registerModelQueue(model::MutationQueue* queue);

  void incrementSetpoint();
  void decrementSetpoint();
  void setSetpoint(int setpoint);

  void turnDisplayOff();
  void turnDisplayOn();
  void setBrightnessPercent(int percent);
  int getBrightnessPercent() const;

  ~DisplayController();
};

#endif
