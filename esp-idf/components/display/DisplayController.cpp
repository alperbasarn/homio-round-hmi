#include "DisplayController.h"
#include "DisplayPanelBackend.h"
#include "KnobController.h"
#include "SoundController.h"
#include "LightController.h"
#include "ModeController.h"
#include "InitializationScreen.h"
#include "EnvironmentInfoScreen.h"
#include "DeviceInfoScreen.h"
#include "LvglDisplay.h"
#include "MediaController.h"
#include "SleepHandler.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal_config.h"
#include "model/AppScreen.h"
#include "model/KnobModel.h"
#include "model/NavigationModel.h"
#include <cstdlib>

static const char *TAG = "DisplayController";
static constexpr int64_t INIT_SEQUENCE_DURATION_MS = 2500;
static constexpr int64_t INIT_COMPLETE_HOLD_MS = 400;
static constexpr int64_t SHORT_HOLD_SOUND_MAX_MS = 3000;

namespace {

const char* modeToString(Mode mode) {
    switch (mode) {
        case INITIALIZATION:
            return "INITIALIZATION";
        case INFO:
            return "INFO";
        case DEVICE_INFO:
            return "DEVICE_INFO";
        case CALIBRATE_ORIENTATION:
            return "CALIBRATE_ORIENTATION";
        case SOUND:
            return "SOUND";
        case LIGHT:
            return "LIGHT";
        case HOME:
            return "HOME";
        case SLEEP:
            return "SLEEP";
        default:
            return "UNKNOWN";
    }
}

constexpr const char* SWIPE_LEFT_EFFECT_FILE = "swipe_left";
constexpr const char* SWIPE_RIGHT_EFFECT_FILE = "swipe_right";
constexpr const char* SWIPE_UP_EFFECT_FILE = "swipe_up";
constexpr const char* SWIPE_DOWN_EFFECT_FILE = "swipe_down";

}  // namespace

DisplayController::DisplayController(TouchPanel* touch, DisplayPanelBackend* displayPanelBackend)
  : touchPanel(touch), knobController(nullptr), soundController(nullptr),
    lightController(nullptr), modeController(nullptr), initializationScreen(nullptr),
    infoScreen(nullptr), deviceInfoScreen(nullptr), mediaController(nullptr), panelBackend(displayPanelBackend), currentMode(INITIALIZATION), lastActivityTime(0), displayIsOn(true),
    initializationProgress(0), initializationStartTime(0), initializationCompleteTime(0), modelQueue(nullptr) {
}

DisplayController::~DisplayController() {
    // Note: This class doesn't own the controller pointers, so it doesn't delete them.
}

void DisplayController::init() {
    initializationProgress = 0;
    initializationStartTime = 0;
    initializationCompleteTime = 0;
    setMode(INITIALIZATION);
    if (initializationScreen) {
        initializationScreen->reset();
        initializationScreen->setProgress(0);
    }
    resetActivityTime();
}

void DisplayController::update() {
    bool hasActivityEvent = false;
    bool shouldRunLvglTask = false;

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
                const int64_t holdMs = touchPanel->getLastPressDurationMs();
                if (holdMs <= SHORT_HOLD_SOUND_MAX_MS) {
                    mediaController->requestTouchReleaseEffect();
                }
            }
            if (mediaController && touchPanel->getHasNewRelease()) {
                mediaController->requestTouchPressEffect();
            }
        }
        if (touchPanel->getHasNewGesture() && mediaController) {
            const touch_gesture_t gesture = touchPanel->getLastGesture();
            switch (gesture) {
                case GESTURE_SWIPE_LEFT:
                    mediaController->requestSoundEffect(SWIPE_LEFT_EFFECT_FILE);
                    break;
                case GESTURE_SWIPE_RIGHT:
                    mediaController->requestSoundEffect(SWIPE_RIGHT_EFFECT_FILE);
                    break;
                case GESTURE_SWIPE_UP:
                    mediaController->requestSoundEffect(SWIPE_UP_EFFECT_FILE);
                    break;
                case GESTURE_SWIPE_DOWN:
                    mediaController->requestSoundEffect(SWIPE_DOWN_EFFECT_FILE);
                    break;
                default:
                    break;
            }
        }
    }

    if (knobController) {
        knobController->update();
        if (knobController->getHasNewMessage()) {
            hasActivityEvent = true;
            resetActivityTime();
            // CTRL-01: CommandHandler consumes the same token from its own task; two consumers per
            // detent until InputRouter owns the link (Phase 2).
            ESP_LOGI(TAG, "Knob token '%s' consumed by DisplayController", knobController->getReceivedCommand().c_str());
            postKnobToken(knobController->getReceivedCommand());   // INPT-05: the model sees every token
        }
    }

    // Wake from sleep on any user activity.
    if (currentMode == SLEEP && hasActivityEvent) {
        setMode(INFO);
        return;
    }
    
    switch (currentMode) {
        case INITIALIZATION:
            if (initializationScreen) {
                int64_t now = esp_timer_get_time() / 1000;
                if (initializationStartTime == 0) {
                    initializationStartTime = now;
                    ESP_LOGI(TAG, "Initialization screen timing started");
                }
                int64_t elapsed = now - initializationStartTime;
                int progress = static_cast<int>((elapsed * 100) / INIT_SEQUENCE_DURATION_MS);
                if (progress > 100) {
                    progress = 100;
                }
                if (progress != initializationProgress) {
                    initializationProgress = progress;
                    initializationScreen->setProgress(initializationProgress);
                    postNavigation(static_cast<int>(model::NavigationModel::Field::InitProgress), initializationProgress);
                    if (initializationProgress >= 100 && initializationCompleteTime == 0) {
                        initializationCompleteTime = now;
                    }
                }
                initializationScreen->updateScreen();
            }
            break;

        case INFO:
            if (infoScreen) {
                infoScreen->update();
                shouldRunLvglTask = true;
                // Check if user wants to go to device info screen (swipe up)
                if (infoScreen->isDeviceInfoRequested()) {
                    infoScreen->resetDeviceInfoRequest();
                    setMode(DEVICE_INFO);
                }
                // Check if user wants to go to mode selection
                else if (infoScreen->isPageBackRequested()) {
                    infoScreen->resetPageBackRequest();
                    if (modeController) {
                        modeController->setActive(true);
                    }
                    setMode(HOME);
                }
            }
            break;

        case DEVICE_INFO:
            if (deviceInfoScreen) {
                deviceInfoScreen->update();
                shouldRunLvglTask = true;
                // Check if user wants to go back to environment info (swipe down)
                if (deviceInfoScreen->isPageBackRequested()) {
                    deviceInfoScreen->resetPageBackRequest();
                    setMode(INFO);
                }
            }
            break;

        case SOUND:
            if (soundController) {
                // Handle knob rotation
                if (knobController && knobController->getHasNewMessage()) {
                    std::string cmd = knobController->getReceivedCommand();
                    if (cmd == "+") {
                        soundController->incrementSetpoint();
                    } else if (cmd == "-") {
                        soundController->decrementSetpoint();
                    }
                }
                soundController->updateScreen();
                // Check if user wants to go back to mode selection
                if (soundController->isPageBackRequested()) {
                    soundController->resetPageBackRequest();
                    if (modeController) {
                        modeController->setActive(true);
                    }
                    setMode(HOME);
                }
            }
            break;

        case LIGHT:
            if (lightController) {
                // Handle knob rotation
                if (knobController && knobController->getHasNewMessage()) {
                    std::string cmd = knobController->getReceivedCommand();
                    if (cmd == "+") {
                        lightController->incrementSetpoint();
                    } else if (cmd == "-") {
                        lightController->decrementSetpoint();
                    }
                }
                lightController->updateScreen();
                // Check if user wants to go back to mode selection
                if (lightController->isPageBackRequested()) {
                    lightController->resetPageBackRequest();
                    if (modeController) {
                        modeController->setActive(true);
                    }
                    setMode(HOME);
                }
            }
            break;

        case HOME:
            if (modeController) {
                // Handle knob rotation to switch between modes
                if (knobController && knobController->getHasNewMessage()) {
                    std::string cmd = knobController->getReceivedCommand();
                    if (cmd == "+") {
                        modeController->nextMode();
                        postNavigation(static_cast<int>(model::NavigationModel::Field::ModeStep), 1);
                    } else if (cmd == "-") {
                        modeController->previousMode();
                        postNavigation(static_cast<int>(model::NavigationModel::Field::ModeStep), -1);
                    }
                }
                modeController->updateScreen();

                // Check if user selected a mode
                if (modeController->isModeSelected()) {
                    modeController->resetModeSelected();
                    ModeController::Mode selectedMode = modeController->getCurrentMode();
                    modeController->setActive(false);
                    {
                        // The wheel's entries map onto KnobMode: MODE_LIGHT -> LIGHT,
                        // MODE_TEMPERATURE -> CLIMATE, MODE_SOUND -> MEDIA.
                        model::KnobMode knobMode = model::KnobMode::Media;
                        if (selectedMode == ModeController::MODE_LIGHT) knobMode = model::KnobMode::Light;
                        if (selectedMode == ModeController::MODE_TEMPERATURE) knobMode = model::KnobMode::Climate;
                        postNavigation(static_cast<int>(model::NavigationModel::Field::Mode), static_cast<int32_t>(knobMode));
                    }

                    if (selectedMode == ModeController::MODE_SOUND) {
                        setMode(SOUND);
                    } else if (selectedMode == ModeController::MODE_LIGHT) {
                        setMode(LIGHT);
                    }
                    // Temperature mode not yet implemented
                }

                // Check if user wants to go back to info screen
                if (modeController->isPageBackRequested()) {
                    modeController->resetPageBackRequest();
                    modeController->setActive(false);
                    setMode(INFO);
                }
            }
            break;

        default:
            // Handle other modes or do nothing
            break;
    }

    if (shouldRunLvglTask && LvglDisplay::isInitialized()) {
        LvglDisplay::taskHandler();
    }

    checkActivityAndAutoSwitch();
}

void DisplayController::setMode(Mode mode) {
    if (currentMode == mode) return;

    ESP_LOGI(TAG, "Changing mode from %s to %s", modeToString(currentMode), modeToString(mode));
    Mode previousMode = currentMode;
    transitionToMode(mode);
    currentMode = mode;
    postNavigationScreen(mode);
    if (mode == SLEEP) {
        turnDisplayOff();
    }
    if (mode == INITIALIZATION) {
        initializationProgress = 0;
        initializationStartTime = 0;
        initializationCompleteTime = 0;
        if (initializationScreen) {
            initializationScreen->reset();
            initializationScreen->setProgress(0);
        }
    }
    // Force full screen redraw when waking from SLEEP to INFO
    if (mode == INFO && previousMode == SLEEP) {
        if (infoScreen) {
            infoScreen->resetScreen();
        }
    }
    // Only reset activity when transitioning to an active mode, not when entering SLEEP.
    // Resetting on SLEEP would immediately wake the display and restore CPU frequency.
    if (mode != SLEEP) {
        resetActivityTime();
    }
}

Mode DisplayController::getMode() const {
    return currentMode;
}

void DisplayController::registerKnobController(KnobController* knob) {
    knobController = knob;
}

void DisplayController::registerModelQueue(model::MutationQueue* queue) {
    modelQueue = queue;
}

void DisplayController::postKnobToken(const std::string& token) {
    if (modelQueue == nullptr) return;
    const uint32_t atMs = static_cast<uint32_t>(esp_timer_get_time() / 1000);
    using KnobField = model::KnobModel::Field;
    if (token == "+") {
        modelQueue->post(model::KnobModel::make(KnobField::Delta, 1, atMs));
    } else if (token == "-") {
        modelQueue->post(model::KnobModel::make(KnobField::Delta, -1, atMs));
    } else if (token.rfind("setpoint=", 0) == 0) {
        // The burst form (FOC.ino handleIncrementDecrement) is dropped by the command grammar
        // today; the model turns it into a clamped delta (D5, KnobModel::burstToDelta).
        const char* digits = token.c_str() + 9;
        char* end = nullptr;
        const long value = std::strtol(digits, &end, 10);
        if (end != digits && *end == '\0') {
            modelQueue->post(model::KnobModel::make(KnobField::Burst, static_cast<int32_t>(value), atMs));
        }
    } else {
        modelQueue->post(model::KnobModel::make(KnobField::LinkActivity, 0, atMs));
    }
}

void DisplayController::postNavigation(int field, int32_t value) {
    if (modelQueue == nullptr) return;
    modelQueue->post(model::NavigationModel::make(static_cast<model::NavigationModel::Field>(field), value,
                                                  static_cast<uint32_t>(esp_timer_get_time() / 1000)));
}

void DisplayController::postNavigationScreen(Mode mode) {
    model::AppScreen screen = model::AppScreen::Count;
    switch (mode) {
        case INITIALIZATION: screen = model::AppScreen::Boot; break;
        case INFO: screen = model::AppScreen::Info; break;
        case DEVICE_INFO: screen = model::AppScreen::DeviceInfo; break;
        case HOME: screen = model::AppScreen::ModeWheel; break;
        case SOUND: screen = model::AppScreen::Media; break;
        case LIGHT: screen = model::AppScreen::Light; break;
        case CALIBRATE_ORIENTATION:
        case SLEEP:
        default:
            return;  // D8: SLEEP is a power state, not a screen; the model keeps the last real one
    }
    postNavigation(static_cast<int>(model::NavigationModel::Field::ActiveScreen), static_cast<int32_t>(screen));
}

void DisplayController::registerSoundController(SoundController* sound) {
    soundController = sound;
}

void DisplayController::registerLightController(LightController* light) {
    lightController = light;
}

void DisplayController::registerModeController(ModeController* mode) {
    modeController = mode;
}

void DisplayController::registerInitializationScreen(InitializationScreen* init) {
    initializationScreen = init;
}

void DisplayController::registerInfoScreen(EnvironmentInfoScreen* info) {
    infoScreen = info;
}

void DisplayController::registerDeviceInfoScreen(DeviceInfoScreen* deviceInfo) {
    deviceInfoScreen = deviceInfo;
}

void DisplayController::registerMediaController(MediaController* media) {
    mediaController = media;
}

void DisplayController::incrementSetpoint() {
    switch (currentMode) {
        case SOUND:
            if (soundController) soundController->incrementSetpoint();
            break;
        case LIGHT:
            if (lightController) lightController->incrementSetpoint();
            break;
        default:
            break;
    }
}

void DisplayController::decrementSetpoint() {
    switch (currentMode) {
        case SOUND:
            if (soundController) soundController->decrementSetpoint();
            break;
        case LIGHT:
            if (lightController) lightController->decrementSetpoint();
            break;
        default:
            break;
    }
}

void DisplayController::setSetpoint(int setpoint) {
    switch (currentMode) {
        case SOUND:
            if (soundController) soundController->updateSetpoint(setpoint);
            break;
        case LIGHT:
            if (lightController) lightController->setSetpoint(setpoint);
            break;
        default:
            break;
    }
}

void DisplayController::transitionToMode(Mode newMode, int transitionType) {
    // LVGL-managed screens (INFO, DEVICE_INFO, COMMISSIONING) have their own
    // full-screen root with a black background that covers the previous content.
    // Skipping a forced clear avoids a visible "flash to black then progressive redraw"
    // caused by LVGL rendering in 50-line bands on the 466px display.
    switch (newMode) {
        case INFO:
        case DEVICE_INFO:
            // Let LVGL handle the screen transition — its root bg clears the old content.
            break;
        default:
            // LVGL-managed roots cover the previous content when they become active.
            break;
    }
}

void DisplayController::checkActivityAndAutoSwitch() {
    int64_t now = esp_timer_get_time() / 1000;
    int64_t inactivity_ms = now - static_cast<int64_t>(lastActivityTime);

    if (currentMode == INITIALIZATION) {
        if (initializationProgress >= 100 &&
            initializationCompleteTime > 0 &&
            (now - initializationCompleteTime) >= INIT_COMPLETE_HOLD_MS) {
            setMode(INFO);
        }
        return;
    }

    if (inactivity_ms > INACTIVITY_TIMEOUT_MS) {
        if (currentMode != SLEEP) {
            setMode(SLEEP);
        }
    }
}

void DisplayController::resetActivityTime() {
    lastActivityTime = esp_timer_get_time() / 1000;
    postNavigation(static_cast<int>(model::NavigationModel::Field::Activity), 0);
    if (!displayIsOn) {
        turnDisplayOn();
    }

    // Register activity with the sleep handler for power management
    SleepHandler* sleepHandler = SleepHandler::getInstance();
    if (sleepHandler) {
        sleepHandler->registerActivity();
    }
}

void DisplayController::turnDisplayOff() {
    if (!displayIsOn) return;
    if (panelBackend != nullptr) {
        panelBackend->turnOff();
    }
    displayIsOn = false;
    ESP_LOGI(TAG, "Display turned off");
}

void DisplayController::turnDisplayOn() {
    if (displayIsOn) return;
    if (panelBackend != nullptr) {
        panelBackend->turnOn();
    }
    displayIsOn = true;
    ESP_LOGI(TAG, "Display turned on");
}
