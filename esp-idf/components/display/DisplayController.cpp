#include "DisplayController.h"
#include "KnobController.h"
#include "SoundController.h"
#include "LightController.h"
#include "ModeController.h"
#include "InitializationScreen.h"
#include "EnvironmentInfoScreen.h"
#include "DeviceInfoScreen.h"
#include "MediaController.h"
#include "SleepHandler.h"
#include "LvglDisplay.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cctype>

static const char *TAG = "DisplayController";
static constexpr int64_t INIT_SEQUENCE_DURATION_MS = 2500;
static constexpr int64_t INIT_COMPLETE_HOLD_MS = 400;
static constexpr int64_t SHORT_HOLD_SOUND_MAX_MS = 3000;

namespace {

const char* modeToString(Mode mode) {
    switch (mode) {
        case INITIALIZATION:       return "INITIALIZATION";
        case INFO:                 return "INFO";
        case DEVICE_INFO:          return "DEVICE_INFO";
        case CALIBRATE_ORIENTATION:return "CALIBRATE_ORIENTATION";
        case SOUND:                return "SOUND";
        case LIGHT:                return "LIGHT";
        case HOME:                 return "HOME";
        case SLEEP:                return "SLEEP";
        default:                   return "UNKNOWN";
    }
}

constexpr const char* SWIPE_LEFT_EFFECT_FILE  = "swipe_left";
constexpr const char* SWIPE_RIGHT_EFFECT_FILE = "swipe_right";
constexpr const char* SWIPE_UP_EFFECT_FILE    = "swipe_up";
constexpr const char* SWIPE_DOWN_EFFECT_FILE  = "swipe_down";

std::string normalizeScreenName(const std::string& value) {
    std::string n = value;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return n;
}

}  // namespace

DisplayController::DisplayController(TouchPanel* touch)
  : touchPanel(touch), knobController(nullptr), soundController(nullptr),
    lightController(nullptr), modeController(nullptr), initializationScreen(nullptr),
    infoScreen(nullptr), deviceInfoScreen(nullptr), mediaController(nullptr),
    currentMode(INITIALIZATION), lastActivityTime(0), initializationStartTime(0),
    displayIsOn(true), initializationProgress(0), initializationCompleteTime(0),
    pendingModeRequest(-1) {
}

DisplayController::~DisplayController() {}

void DisplayController::init() {
    LvglDisplay::fillScreen(0x000000);
    initializationProgress = 0;
    initializationCompleteTime = 0;
    initializationStartTime = esp_timer_get_time() / 1000;
    // currentMode is already INITIALIZATION — no setMode() call to avoid re-enter
    if (initializationScreen) {
        initializationScreen->reset();
        initializationScreen->setProgress(0);
    }
    resetActivityTime();
}

void DisplayController::update() {
    const int pendingMode = pendingModeRequest.exchange(-1);
    if (pendingMode >= 0) {
        setMode(static_cast<Mode>(pendingMode));
    }

    bool hasActivityEvent = false;

    if (touchPanel) {
        touchPanel->handleTouchPanel();
        if (touchPanel->getHasNewPress()) {
            hasActivityEvent = true;
            resetActivityTime();
        }
        if (touchPanel->getHasNewRelease() || touchPanel->getHasNewHoldRelease()) {
            hasActivityEvent = true;
            resetActivityTime();
            if (mediaController && touchPanel->getHasNewHoldRelease()) {
                if (touchPanel->getLastPressDurationMs() <= SHORT_HOLD_SOUND_MAX_MS)
                    mediaController->requestTouchReleaseEffect();
            }
            if (mediaController && touchPanel->getHasNewRelease())
                mediaController->requestTouchPressEffect();
        }
        if (touchPanel->getHasNewGesture() && mediaController) {
            switch (touchPanel->getLastGesture()) {
                case GESTURE_SWIPE_LEFT:  mediaController->requestSoundEffect(SWIPE_LEFT_EFFECT_FILE);  break;
                case GESTURE_SWIPE_RIGHT: mediaController->requestSoundEffect(SWIPE_RIGHT_EFFECT_FILE); break;
                case GESTURE_SWIPE_UP:    mediaController->requestSoundEffect(SWIPE_UP_EFFECT_FILE);    break;
                case GESTURE_SWIPE_DOWN:  mediaController->requestSoundEffect(SWIPE_DOWN_EFFECT_FILE);  break;
                default: break;
            }
        }
    }

    if (knobController) {
        knobController->update();
        if (knobController->getHasNewMessage()) {
            hasActivityEvent = true;
            resetActivityTime();
        }
    }

    if (currentMode == SLEEP && hasActivityEvent) {
        setMode(INFO);
        return;
    }

    switch (currentMode) {
        case INITIALIZATION:
            if (initializationScreen) {
                int64_t now = esp_timer_get_time() / 1000;
                int progress = static_cast<int>((now - initializationStartTime) * 100 / INIT_SEQUENCE_DURATION_MS);
                if (progress > 100) progress = 100;
                if (progress != initializationProgress) {
                    initializationProgress = progress;
                    initializationScreen->setProgress(initializationProgress);
                    if (initializationProgress >= 100 && initializationCompleteTime == 0)
                        initializationCompleteTime = now;
                }
                initializationScreen->updateScreen();
            }
            break;

        case INFO:
            if (infoScreen) {
                infoScreen->update();
                if (infoScreen->isDeviceInfoRequested()) {
                    infoScreen->resetDeviceInfoRequest();
                    setMode(DEVICE_INFO);
                } else if (infoScreen->isPageBackRequested()) {
                    infoScreen->resetPageBackRequest();
                    setMode(HOME);
                }
            }
            break;

        case DEVICE_INFO:
            if (deviceInfoScreen) {
                deviceInfoScreen->update();
                if (deviceInfoScreen->isPageBackRequested()) {
                    deviceInfoScreen->resetPageBackRequest();
                    setMode(INFO);
                }
            }
            break;

        case SOUND:
            if (soundController) {
                if (knobController && knobController->getHasNewMessage()) {
                    const std::string cmd = knobController->getReceivedCommand();
                    if (cmd == "+") soundController->incrementSetpoint();
                    else if (cmd == "-") soundController->decrementSetpoint();
                }
                soundController->updateScreen();
                if (soundController->isPageBackRequested()) {
                    soundController->resetPageBackRequest();
                    setMode(HOME);
                }
            }
            break;

        case LIGHT:
            if (lightController) {
                if (knobController && knobController->getHasNewMessage()) {
                    const std::string cmd = knobController->getReceivedCommand();
                    if (cmd == "+") lightController->incrementSetpoint();
                    else if (cmd == "-") lightController->decrementSetpoint();
                }
                lightController->updateScreen();
                if (lightController->isPageBackRequested()) {
                    lightController->resetPageBackRequest();
                    setMode(HOME);
                }
            }
            break;

        case HOME:
            if (modeController) {
                if (knobController && knobController->getHasNewMessage()) {
                    const std::string cmd = knobController->getReceivedCommand();
                    if (cmd == "+") modeController->nextMode();
                    else if (cmd == "-") modeController->previousMode();
                }
                modeController->updateScreen();

                if (modeController->isModeSelected()) {
                    modeController->resetModeSelected();
                    const ModeController::Mode sel = modeController->getCurrentMode();
                    modeController->deactivate();
                    if (sel == ModeController::MODE_SOUND) setMode(SOUND);
                    else if (sel == ModeController::MODE_LIGHT) setMode(LIGHT);
                }
                if (modeController->isPageBackRequested()) {
                    modeController->resetPageBackRequest();
                    modeController->deactivate();
                    setMode(INFO);
                }
            }
            break;

        default:
            break;
    }

    checkActivityAndAutoSwitch();
}

void DisplayController::setMode(Mode mode) {
    if (currentMode == mode) return;

    ESP_LOGI(TAG, "Mode: %s → %s", modeToString(currentMode), modeToString(mode));

    // Hide current LVGL screen before leaving it
    if (currentMode == INITIALIZATION && initializationScreen)
        initializationScreen->hideScreen();
    if (currentMode == INFO && infoScreen)
        infoScreen->deactivate();
    if (currentMode == DEVICE_INFO && deviceInfoScreen)
        deviceInfoScreen->deactivate();
    if (currentMode == HOME && modeController)
        modeController->deactivate();

    transitionToMode(mode);
    currentMode = mode;

    if (mode == SLEEP) turnDisplayOff();

    if (mode == INITIALIZATION) {
        initializationProgress = 0;
        initializationCompleteTime = 0;
        initializationStartTime = esp_timer_get_time() / 1000;
        if (initializationScreen) {
            initializationScreen->reset();
            initializationScreen->setProgress(0);
        }
    }
    if (mode == INFO && infoScreen)         infoScreen->resetScreen();
    if (mode == DEVICE_INFO && deviceInfoScreen) deviceInfoScreen->resetScreen();

    // Always activate home screen when entering HOME mode
    if (mode == HOME && modeController)     modeController->setActive(true);

    if (mode != SLEEP) resetActivityTime();
}

Mode DisplayController::getMode() const { return currentMode; }

bool DisplayController::showNamedScreen(const std::string& screenName) {
    const std::string n = normalizeScreenName(screenName);
    Mode target = currentMode;

    if (n == "info" || n == "infoscreen" || n == "environment" || n == "environmentinfoscreen")
        target = INFO;
    else if (n == "deviceinfo" || n == "deviceinfoscreen")
        target = DEVICE_INFO;
    else if (n == "home")
        target = HOME;
    else if (n == "sound" || n == "soundcontroller")
        target = SOUND;
    else if (n == "light" || n == "lightcontroller")
        target = LIGHT;
    else if (n == "calibrate" || n == "calibrate_orientation" || n == "calibrateorientation")
        target = CALIBRATE_ORIENTATION;
    else
        return false;

    pendingModeRequest.store(static_cast<int>(target));
    return true;
}

std::string DisplayController::getModeName() const {
    switch (currentMode) {
        case INFO:                 return "info";
        case DEVICE_INFO:          return "deviceInfo";
        case HOME:                 return "home";
        case SOUND:                return "sound";
        case LIGHT:                return "light";
        case CALIBRATE_ORIENTATION:return "calibrate";
        case SLEEP:                return "sleep";
        case INITIALIZATION:
        default:                   return "initialization";
    }
}

void DisplayController::registerKnobController(KnobController* k)      { knobController = k; }
void DisplayController::registerSoundController(SoundController* s)    { soundController = s; }
void DisplayController::registerLightController(LightController* l)    { lightController = l; }
void DisplayController::registerModeController(ModeController* m)      { modeController = m; }
void DisplayController::registerInitializationScreen(InitializationScreen* i) { initializationScreen = i; }
void DisplayController::registerInfoScreen(EnvironmentInfoScreen* info)       { infoScreen = info; }
void DisplayController::registerDeviceInfoScreen(DeviceInfoScreen* di)        { deviceInfoScreen = di; }
void DisplayController::registerMediaController(MediaController* m)    { mediaController = m; }

void DisplayController::incrementSetpoint() {
    if (currentMode == SOUND && soundController)  soundController->incrementSetpoint();
    if (currentMode == LIGHT && lightController)  lightController->incrementSetpoint();
}
void DisplayController::decrementSetpoint() {
    if (currentMode == SOUND && soundController)  soundController->decrementSetpoint();
    if (currentMode == LIGHT && lightController)  lightController->decrementSetpoint();
}
void DisplayController::setSetpoint(int sp) {
    if (currentMode == SOUND && soundController)  soundController->updateSetpoint(sp);
    if (currentMode == LIGHT && lightController)  lightController->setSetpoint(sp);
}

void DisplayController::transitionToMode(Mode newMode, int /*transitionType*/) {
    // All screens are LVGL-managed — they clear their own background.
    // For non-LVGL screens (SOUND, LIGHT) clear via DrawAPI.
    switch (newMode) {
        case INITIALIZATION:
        case INFO:
        case DEVICE_INFO:
        case HOME:
            break;  // LVGL root handles clearing
        default:
            LvglDisplay::fillScreen(0x000000);
            break;
    }
}

void DisplayController::checkActivityAndAutoSwitch() {
    int64_t now = esp_timer_get_time() / 1000;

    if (currentMode == INITIALIZATION) {
        if (initializationProgress >= 100 &&
            initializationCompleteTime > 0 &&
            (now - initializationCompleteTime) >= INIT_COMPLETE_HOLD_MS) {
            setMode(INFO);
        }
        return;
    }

    if ((now - static_cast<int64_t>(lastActivityTime)) > INACTIVITY_TIMEOUT_MS) {
        if (currentMode != SLEEP) setMode(SLEEP);
    }
}

void DisplayController::resetActivityTime() {
    lastActivityTime = esp_timer_get_time() / 1000;
    if (!displayIsOn) turnDisplayOn();
    SleepHandler* sh = SleepHandler::getInstance();
    if (sh) sh->registerActivity();
}

void DisplayController::turnDisplayOff() {
    if (!displayIsOn) return;
#if defined(LCD_BL_PIN) && (LCD_BL_PIN != -1)
    for (int i = 255; i >= 0; i -= 5) {
        LvglDisplay::setBrightness(static_cast<uint8_t>(i));
        vTaskDelay(pdMS_TO_TICKS(1));
    }
#endif
    displayIsOn = false;
    ESP_LOGI(TAG, "Display off");
}

void DisplayController::turnDisplayOn() {
    if (displayIsOn) return;
#if defined(LCD_BL_PIN) && (LCD_BL_PIN != -1)
    for (int i = 0; i <= 255; i += 5) {
        LvglDisplay::setBrightness(static_cast<uint8_t>(i));
        vTaskDelay(pdMS_TO_TICKS(1));
    }
#endif
    displayIsOn = true;
    ESP_LOGI(TAG, "Display on");
}
