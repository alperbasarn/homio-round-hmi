#include "DisplayController.h"
#include "KnobController.h"
#include "SoundController.h"
#include "LightController.h"
#include "ModeController.h"
#include "InitializationScreen.h"
#include "EnvironmentInfoScreen.h"
#include "DeviceInfoScreen.h"
#include "PlaceholderPageScreen.h"
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
#include <limits>

static const char *TAG = "DisplayController";
static constexpr int64_t INIT_SEQUENCE_DURATION_MS = 2500;
static constexpr int64_t INIT_COMPLETE_HOLD_MS = 400;
static constexpr int64_t SHORT_HOLD_SOUND_MAX_MS = 3000;

namespace {

constexpr const char* SWIPE_LEFT_EFFECT_FILE  = "swipe_left";
constexpr const char* SWIPE_RIGHT_EFFECT_FILE = "swipe_right";
constexpr const char* SWIPE_UP_EFFECT_FILE    = "swipe_up";
constexpr const char* SWIPE_DOWN_EFFECT_FILE  = "swipe_down";
constexpr int kNoPageRequest = std::numeric_limits<int>::max();

struct MatrixPageDescriptor {
    int x;
    int y;
    Mode mode;
    const char* name;
    const char* title;
    const char* subtitle;
};

constexpr MatrixPageDescriptor kMatrixPages[] = {
    {0, -1, DEVICE_INFO, "deviceInfo", "Device Info", "Status and software telemetry"},
    {0,  0, INFO,        "info", "Environment Info", "Clock and temperatures"},
    {0,  1, MATRIX_PAGE, "timer", "Timer / Chronometer", "Placeholder. Tap-to-enter flow comes later."},
    {0,  2, MATRIX_PAGE, "light", "Light Control", "Placeholder. Tap-to-enter flow comes later."},
    {0,  3, MATRIX_PAGE, "sound", "Sound Control", "Placeholder. Tap-to-enter flow comes later."},
    {0,  4, MATRIX_PAGE, "temperature", "Temperature Control", "Placeholder. Tap-to-enter flow comes later."},
    {0,  5, MATRIX_PAGE, "pc", "PC Control", "Placeholder. Tap-to-enter flow comes later."},
};

const char* modeToString(Mode mode) {
    switch (mode) {
        case INITIALIZATION:       return "INITIALIZATION";
        case INFO:                 return "INFO";
        case DEVICE_INFO:          return "DEVICE_INFO";
        case MATRIX_PAGE:          return "MATRIX_PAGE";
        case CALIBRATE_ORIENTATION:return "CALIBRATE_ORIENTATION";
        case SOUND:                return "SOUND";
        case LIGHT:                return "LIGHT";
        case HOME:                 return "HOME";
        case SLEEP:                return "SLEEP";
        default:                   return "UNKNOWN";
    }
}

std::string normalizeScreenName(const std::string& value) {
    std::string n = value;
    std::transform(n.begin(), n.end(), n.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return n;
}

int packPageRequest(int x, int y) {
    return ((x & 0xFFFF) << 16) | (y & 0xFFFF);
}

int unpackPageX(int packed) {
    return static_cast<int16_t>((packed >> 16) & 0xFFFF);
}

int unpackPageY(int packed) {
    return static_cast<int16_t>(packed & 0xFFFF);
}

const MatrixPageDescriptor* findMatrixPage(int x, int y) {
    for (const MatrixPageDescriptor& page : kMatrixPages) {
        if (page.x == x && page.y == y) {
            return &page;
        }
    }
    return nullptr;
}

const MatrixPageDescriptor* findMatrixPageByName(const std::string& normalized) {
    for (const MatrixPageDescriptor& page : kMatrixPages) {
        if (normalizeScreenName(page.name) == normalized) {
            return &page;
        }
    }

    if (normalized == "environment" || normalized == "environmentinfo" ||
        normalized == "environmentinfoscreen" || normalized == "infoscreen") {
        return findMatrixPage(0, 0);
    }
    if (normalized == "deviceinfo" || normalized == "deviceinfoscreen") {
        return findMatrixPage(0, -1);
    }
    if (normalized == "chronometer") {
        return findMatrixPage(0, 1);
    }
    if (normalized == "temp" || normalized == "temperaturecontrol") {
        return findMatrixPage(0, 4);
    }

    return nullptr;
}

}  // namespace

DisplayController::DisplayController(TouchPanel* touch)
  : touchPanel(touch), knobController(nullptr), soundController(nullptr),
    lightController(nullptr), modeController(nullptr), initializationScreen(nullptr),
    infoScreen(nullptr), deviceInfoScreen(nullptr), placeholderScreen(nullptr), mediaController(nullptr),
    currentMode(INITIALIZATION), lastActivityTime(0), initializationStartTime(0),
    displayIsOn(true), initializationProgress(0), initializationCompleteTime(0),
    currentPageX(0), currentPageY(0), pendingModeRequest(-1), pendingPageRequest(kNoPageRequest) {
}

DisplayController::~DisplayController() {}

void DisplayController::init() {
    LvglDisplay::fillScreen(0x000000);
    initializationProgress = 0;
    initializationCompleteTime = 0;
    initializationStartTime = esp_timer_get_time() / 1000;
    currentPageX = 0;
    currentPageY = 0;
    if (initializationScreen) {
        initializationScreen->reset();
        initializationScreen->setProgress(0);
    }
    resetActivityTime();
}

void DisplayController::update() {
    const int pendingPage = pendingPageRequest.exchange(kNoPageRequest);
    if (pendingPage != kNoPageRequest) {
        navigateToMatrixPage(unpackPageX(pendingPage), unpackPageY(pendingPage));
    }

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
                if (touchPanel->getLastPressDurationMs() <= SHORT_HOLD_SOUND_MAX_MS) {
                    mediaController->requestTouchReleaseEffect();
                }
            }
            if (mediaController && touchPanel->getHasNewRelease()) {
                mediaController->requestTouchPressEffect();
            }
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
        const MatrixPageDescriptor* page = findMatrixPage(currentPageX, currentPageY);
        setMode(page ? page->mode : INFO);
        return;
    }

    if (touchPanel && touchPanel->getHasNewGesture()) {
        handleMatrixNavigationGesture(touchPanel->getLastGesture());
    }

    switch (currentMode) {
        case INITIALIZATION:
            if (initializationScreen) {
                int64_t now = esp_timer_get_time() / 1000;
                const int64_t initElapsedMs = now - initializationStartTime;
                int progress = static_cast<int>((now - initializationStartTime) * 100 / INIT_SEQUENCE_DURATION_MS);
                if (progress > 100) {
                    progress = 100;
                }
                if (progress != initializationProgress) {
                    initializationProgress = progress;
                    initializationScreen->setProgress(initializationProgress);
                    if (initializationProgress >= 100 && initializationCompleteTime == 0) {
                        initializationCompleteTime = now;
                    }
                }
                initializationScreen->updateScreen();

                // Deterministic handoff from init to home page.
                // Primary condition uses progress+hold; fallback uses elapsed time.
                const bool progressHoldReached =
                    (initializationProgress >= 100) &&
                    (initializationCompleteTime > 0) &&
                    ((now - initializationCompleteTime) >= INIT_COMPLETE_HOLD_MS);
                const bool timeoutFallbackReached =
                    initElapsedMs >= (INIT_SEQUENCE_DURATION_MS + INIT_COMPLETE_HOLD_MS);

                if (progressHoldReached || timeoutFallbackReached) {
                    ESP_LOGI(TAG,
                             "Init handoff direct switch (progress=%d elapsed=%lldms completeAt=%lld)",
                             initializationProgress,
                             static_cast<long long>(initElapsedMs),
                             static_cast<long long>(initializationCompleteTime));
                    // Direct switch is more reliable here than pending-mode handoff.
                    setMode(INFO);
                    return;
                }
            }
            break;

        case INFO:
            if (infoScreen) {
                infoScreen->update();
            }
            break;

        case DEVICE_INFO:
            if (deviceInfoScreen) {
                deviceInfoScreen->update();
            }
            break;

        case MATRIX_PAGE:
            if (placeholderScreen) {
                placeholderScreen->update();
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
                    navigateToMatrixPage(0, 0);
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
                    navigateToMatrixPage(0, 0);
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
                    navigateToMatrixPage(0, 0);
                }
            }
            break;

        default:
            break;
    }

    checkActivityAndAutoSwitch();
}

void DisplayController::setMode(Mode mode) {
    if (mode == INFO) {
        currentPageX = 0;
        currentPageY = 0;
    } else if (mode == DEVICE_INFO) {
        currentPageX = 0;
        currentPageY = -1;
    }

    if (currentMode == mode) {
        if (mode != SLEEP) {
            resetActivityTime();
            resetActiveScreen();
        }
        return;
    }

    ESP_LOGI(TAG, "Mode: %s -> %s", modeToString(currentMode), modeToString(mode));

    if (currentMode == INITIALIZATION && initializationScreen) {
        initializationScreen->hideScreen();
    }
    if (currentMode == INFO && infoScreen) {
        infoScreen->deactivate();
    }
    if (currentMode == DEVICE_INFO && deviceInfoScreen) {
        deviceInfoScreen->deactivate();
    }
    if (currentMode == MATRIX_PAGE && placeholderScreen) {
        placeholderScreen->deactivate();
    }
    if (currentMode == HOME && modeController) {
        modeController->deactivate();
    }

    transitionToMode(mode);
    currentMode = mode;

    if (mode == SLEEP) {
        turnDisplayOff();
    }

    if (mode == INITIALIZATION) {
        initializationProgress = 0;
        initializationCompleteTime = 0;
        initializationStartTime = esp_timer_get_time() / 1000;
        if (initializationScreen) {
            initializationScreen->reset();
            initializationScreen->setProgress(0);
        }
    }
    if (mode == INFO && infoScreen) {
        infoScreen->resetScreen();
    }
    if (mode == DEVICE_INFO && deviceInfoScreen) {
        deviceInfoScreen->resetScreen();
    }
    if (mode == MATRIX_PAGE && placeholderScreen) {
        const MatrixPageDescriptor* page = findMatrixPage(currentPageX, currentPageY);
        if (page != nullptr) {
            placeholderScreen->setContent(page->title, page->subtitle);
        }
        placeholderScreen->activate();
        placeholderScreen->resetScreen();
    }
    if (mode == HOME && modeController) {
        modeController->setActive(true);
    }

    if (mode != SLEEP) {
        resetActivityTime();
    }
}

Mode DisplayController::getMode() const { return currentMode; }

bool DisplayController::showNamedScreen(const std::string& screenName) {
    const std::string n = normalizeScreenName(screenName);
    resetActivityTime();

    const MatrixPageDescriptor* page = findMatrixPageByName(n);
    if (page != nullptr) {
        pendingPageRequest.store(packPageRequest(page->x, page->y));
        return true;
    }

    if (n == "home") {
        pendingPageRequest.store(packPageRequest(0, 0));
        return true;
    }
    if (n == "calibrate" || n == "calibrate_orientation" || n == "calibrateorientation") {
        pendingModeRequest.store(static_cast<int>(CALIBRATE_ORIENTATION));
        return true;
    }
    if (n == "soundcontroller") {
        pendingModeRequest.store(static_cast<int>(SOUND));
        return true;
    }
    if (n == "lightcontroller") {
        pendingModeRequest.store(static_cast<int>(LIGHT));
        return true;
    }

    return false;
}

std::string DisplayController::getModeName() const {
    const MatrixPageDescriptor* page = findMatrixPage(currentPageX, currentPageY);
    switch (currentMode) {
        case INFO:
        case DEVICE_INFO:
        case MATRIX_PAGE:
            return page ? page->name : "info";
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
void DisplayController::registerPlaceholderScreen(PlaceholderPageScreen* p)   { placeholderScreen = p; }
void DisplayController::registerMediaController(MediaController* m)    { mediaController = m; }

void DisplayController::incrementSetpoint() {
    if (currentMode == SOUND && soundController) soundController->incrementSetpoint();
    if (currentMode == LIGHT && lightController) lightController->incrementSetpoint();
}

void DisplayController::decrementSetpoint() {
    if (currentMode == SOUND && soundController) soundController->decrementSetpoint();
    if (currentMode == LIGHT && lightController) lightController->decrementSetpoint();
}

void DisplayController::setSetpoint(int sp) {
    if (currentMode == SOUND && soundController) soundController->updateSetpoint(sp);
    if (currentMode == LIGHT && lightController) lightController->setSetpoint(sp);
}

void DisplayController::transitionToMode(Mode newMode, int /*transitionType*/) {
    switch (newMode) {
        case INITIALIZATION:
        case INFO:
        case DEVICE_INFO:
        case MATRIX_PAGE:
        case HOME:
            break;
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
            navigateToMatrixPage(0, 0);
        }
        return;
    }

    if ((now - static_cast<int64_t>(lastActivityTime)) > INACTIVITY_TIMEOUT_MS) {
        if (currentMode != SLEEP) {
            setMode(SLEEP);
        }
    }
}

void DisplayController::resetActivityTime() {
    lastActivityTime = esp_timer_get_time() / 1000;
    if (!displayIsOn) turnDisplayOn();
    SleepHandler* sh = SleepHandler::getInstance();
    if (sh) {
        sh->registerActivity();
    }
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
#else
    LvglDisplay::setBrightness(255);
#endif
    displayIsOn = true;
    ESP_LOGI(TAG, "Display on");
}

bool DisplayController::handleMatrixNavigationGesture(touch_gesture_t gesture) {
    if (currentMode != INFO && currentMode != DEVICE_INFO && currentMode != MATRIX_PAGE) {
        return false;
    }

    int targetX = currentPageX;
    int targetY = currentPageY;

    switch (gesture) {
        case GESTURE_SWIPE_LEFT:
            targetX -= 1;
            break;
        case GESTURE_SWIPE_RIGHT:
            targetX += 1;
            break;
        case GESTURE_SWIPE_UP:
            targetY += 1;
            break;
        case GESTURE_SWIPE_DOWN:
            targetY -= 1;
            break;
        default:
            return false;
    }

    if (targetX == currentPageX && targetY == currentPageY) {
        return false;
    }

    return navigateToMatrixPage(targetX, targetY);
}

bool DisplayController::navigateToMatrixPage(int x, int y) {
    const MatrixPageDescriptor* page = findMatrixPage(x, y);
    if (page == nullptr) {
        ESP_LOGI(TAG, "No page at matrix index (%d,%d)", x, y);
        return false;
    }

    currentPageX = x;
    currentPageY = y;

    if (currentMode == page->mode) {
        if (page->mode == MATRIX_PAGE && placeholderScreen) {
            placeholderScreen->setContent(page->title, page->subtitle);
        }
        resetActivityTime();
        resetActiveScreen();
        return true;
    }

    setMode(page->mode);
    return true;
}

void DisplayController::resetActiveScreen() {
    switch (currentMode) {
        case INFO:
            if (infoScreen) {
                infoScreen->resetScreen();
            }
            break;
        case DEVICE_INFO:
            if (deviceInfoScreen) {
                deviceInfoScreen->resetScreen();
            }
            break;
        case MATRIX_PAGE:
            if (placeholderScreen) {
                const MatrixPageDescriptor* page = findMatrixPage(currentPageX, currentPageY);
                if (page != nullptr) {
                    placeholderScreen->setContent(page->title, page->subtitle);
                }
                placeholderScreen->resetScreen();
            }
            break;
        default:
            break;
    }
}