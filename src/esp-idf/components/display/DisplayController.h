#ifndef DISPLAY_CONTROLLER_H
#define DISPLAY_CONTROLLER_H

#include "input/InputEvent.h"
#include "model/MutationQueue.h"
#include <string>
#include <atomic>
#include <cstdint>

// Forward declarations
class ModeController;
namespace controllers { class BootScreenController; class ScreenController; class DeviceInfoScreenController; }
class EnvironmentInfoScreen;
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
  controllers::ScreenController* mediaScreen;   // MEDIA-16: the MEDIA screen on the target registry
  controllers::ScreenController* lightScreen;   // LED-05: the LIGHT screen on the registry
  ModeController* modeController;
  controllers::BootScreenController* bootController;   // SCRN-08: the boot ramp and its view
  EnvironmentInfoScreen* infoScreen;
  controllers::DeviceInfoScreenController* deviceInfoScreen;   // SCRN-11: the DEVICE_INFO page
  PlaceholderPageScreen* placeholderScreen;

  Mode currentMode;
  int64_t lastActivityTime;
  bool displayIsOn;
  int brightnessPercent;
  int currentPageX;
  int currentPageY;
  uint32_t lastActivityPostMs;
  std::atomic<int> pendingModeRequest;
  std::atomic<int> pendingPageRequest;
  std::atomic<int> pendingKnobDelta;
  std::atomic<int> pendingBrightnessRequest;  // -1: none; a percent asked for off the display task
  // SCRN-09: set by app_main once every screen and driver is registered; until then the boot
  // handoff stays pending and the display task only runs the boot controller.
  std::atomic<bool> compositionComplete;
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
  // SCRN-23 for one screen: dim, build, pump once, restore, so the first paint of a page is not
  // drawn band by band and no view ever touches the backlight.
  void enterDeviceInfo();
  void exitDeviceInfo();
  void applyBrightnessToPanel();

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

  void registerMediaScreen(controllers::ScreenController* media);
  void registerLightScreen(controllers::ScreenController* light);
  void registerModeController(ModeController* mode);
  void registerBootController(controllers::BootScreenController* boot);
  void markCompositionComplete();
  void registerInfoScreen(EnvironmentInfoScreen* info);
  void registerDeviceInfoScreen(controllers::DeviceInfoScreenController* deviceInfo);
  void registerPlaceholderScreen(PlaceholderPageScreen* placeholder);
  void registerModelQueue(model::MutationQueue* queue);

  // INPT-22: a setpoint command on the command task lands as a delta applied on the display task.
  void requestKnobDelta(int delta);
  // A mode switch asked for off the display task (console, MQTT command): applied in update().
  void requestMode(Mode mode) { pendingModeRequest.store(static_cast<int>(mode)); }
  // The same for a brightness percent (main's setBrightness command): the panel is only written
  // from the display task.
  void requestBrightnessPercent(int percent) { pendingBrightnessRequest.store(percent < 0 ? 0 : (percent > 100 ? 100 : percent)); }

  void turnDisplayOff();
  void turnDisplayOn();
  void setBrightnessPercent(int percent);
  int getBrightnessPercent() const;

  ~DisplayController();
};

#endif
