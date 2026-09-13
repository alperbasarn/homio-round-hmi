#include "DisplayController.h"
#include "SoundController.h"
#include "LightController.h"
#include "ModeController.h"
#include "BootScreenController.h"
#include "EnvironmentInfoScreen.h"
#include "DeviceInfoScreen.h"
#include "PlaceholderPageScreen.h"
#include "LvglDisplay.h"
#include "model/AppScreen.h"
#include "model/KnobModel.h"
#include "model/NavigationModel.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "hal_config.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>

static const char *TAG = "DisplayController";

namespace {

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

DisplayController::DisplayController()
  : soundController(nullptr),
    lightController(nullptr), modeController(nullptr), bootController(nullptr),
    infoScreen(nullptr), deviceInfoScreen(nullptr), placeholderScreen(nullptr),
    currentMode(INITIALIZATION), lastActivityTime(0),
    displayIsOn(true), brightnessPercent(100),
    currentPageX(0), currentPageY(0), lastActivityPostMs(0),
    pendingModeRequest(-1), pendingPageRequest(kNoPageRequest), pendingKnobDelta(0), compositionComplete(false),
    modelQueue(nullptr) {
}

DisplayController::~DisplayController() {}

void DisplayController::init() {
    LvglDisplay::fillScreen(0x000000);
    currentPageX = 0;
    currentPageY = 0;
    // SCRN-08: the boot ramp starts here, once; the view is built under the LVGL lock.
    if (bootController && LvglDisplay::lock(-1)) {
        bootController->onEnter(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        LvglDisplay::unlock();
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

    const int requestedDelta = pendingKnobDelta.exchange(0);
    if (requestedDelta != 0 && currentMode != SLEEP && currentMode != INITIALIZATION) {
        noteActivity();
        applyKnobDelta(requestedDelta);
    }

    // INPT-14: touch and knob arrive through onInput(); the SLEEP exit lives in noteActivity().
    switch (currentMode) {
        case INITIALIZATION:
            if (bootController) {
                // SCRN-08: the controller owns the ramp and renders its view under the lock; the
                // handoff waits for app_main to finish registering everything (SCRN-09).
                const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000);
                if (LvglDisplay::lock(-1)) {
                    bootController->tick(now);
                    LvglDisplay::unlock();
                }
                LvglDisplay::taskHandler();
                if (bootController->hasNavigationRequest() && compositionComplete.load()) {
                    const controllers::NavigationRequest request = bootController->takeNavigationRequest();
                    if (request.valid() && request.screen == model::AppScreen::Info) {
                        ESP_LOGI(TAG, "Boot handoff to INFO (ramp %d%%)", bootController->progressPercent());
                        setMode(INFO);
                        return;
                    }
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
                if (deviceInfoScreen->isPageBackRequested()) {
                    deviceInfoScreen->resetPageBackRequest();
                    navigateToMatrixPage(0, 0);
                    return;
                }
                if (deviceInfoScreen->isPageLeftRequested()) {
                    deviceInfoScreen->resetPageLeftRequest();
                    navigateToMatrixPage(currentPageX, currentPageY - 1);
                    return;
                }
                if (deviceInfoScreen->isPageRightRequested()) {
                    deviceInfoScreen->resetPageRightRequest();
                    navigateToMatrixPage(currentPageX, currentPageY + 1);
                    return;
                }
            }
            break;

        case MATRIX_PAGE:
            if (placeholderScreen) {
                placeholderScreen->update();
            }
            break;

        case SOUND:
            if (soundController) {
                soundController->updateScreen();
                if (soundController->isPageBackRequested()) {
                    soundController->resetPageBackRequest();
                    navigateToMatrixPage(0, 0);
                }
            }
            break;

        case LIGHT:
            if (lightController) {
                lightController->updateScreen();
                if (lightController->isPageBackRequested()) {
                    lightController->resetPageBackRequest();
                    navigateToMatrixPage(0, 0);
                }
            }
            break;

        case HOME:
            if (modeController) {
                modeController->updateScreen();

                if (modeController->isModeSelected()) {
                    modeController->resetModeSelected();
                    const ModeController::Mode sel = modeController->getCurrentMode();
                    modeController->deactivate();
                    {
                        // The wheel entries map onto KnobMode: MODE_LIGHT -> LIGHT,
                        // MODE_TEMPERATURE -> CLIMATE, MODE_SOUND -> MEDIA.
                        model::KnobMode knobMode = model::KnobMode::Media;
                        if (sel == ModeController::MODE_LIGHT) knobMode = model::KnobMode::Light;
                        if (sel == ModeController::MODE_TEMPERATURE) knobMode = model::KnobMode::Climate;
                        postNavigation(static_cast<int>(model::NavigationModel::Field::Mode), static_cast<int32_t>(knobMode));
                    }
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

    if (currentMode == INITIALIZATION && bootController && LvglDisplay::lock(-1)) {
        bootController->onExit(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        LvglDisplay::unlock();
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
    postNavigationScreen(mode);

    if (mode == SLEEP) {
        turnDisplayOff();
    }

    if (mode == INITIALIZATION && bootController && LvglDisplay::lock(-1)) {
        bootController->onEnter(static_cast<uint32_t>(esp_timer_get_time() / 1000));
        LvglDisplay::unlock();
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

void DisplayController::registerSoundController(SoundController* s)    { soundController = s; }
void DisplayController::registerLightController(LightController* l)    { lightController = l; }
void DisplayController::registerModeController(ModeController* m)      { modeController = m; }
void DisplayController::registerBootController(controllers::BootScreenController* boot) { bootController = boot; }
void DisplayController::markCompositionComplete() { compositionComplete.store(true); }
void DisplayController::registerInfoScreen(EnvironmentInfoScreen* info)       { infoScreen = info; }
void DisplayController::registerDeviceInfoScreen(DeviceInfoScreen* di)        { deviceInfoScreen = di; }
void DisplayController::registerPlaceholderScreen(PlaceholderPageScreen* p)   { placeholderScreen = p; }

void DisplayController::requestKnobDelta(int delta) {
    pendingKnobDelta.fetch_add(delta);
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
        return;  // the boot controller decides the handoff; no auto sleep while booting
    }

    if ((now - static_cast<int64_t>(lastActivityTime)) > INACTIVITY_TIMEOUT_MS) {
        if (currentMode != SLEEP) {
            setMode(SLEEP);
        }
    }
}

void DisplayController::resetActivityTime() {
    lastActivityTime = esp_timer_get_time() / 1000;
    // One Activity record per 250 ms is enough for the model; a drag would otherwise post one
    // per frame. PERI-09: the SleepHandler hears about activity from main, not from here.
    const uint32_t nowMs32 = static_cast<uint32_t>(lastActivityTime);
    if (lastActivityPostMs == 0 || static_cast<uint32_t>(nowMs32 - lastActivityPostMs) >= 250) {
        lastActivityPostMs = nowMs32;
        postNavigation(static_cast<int>(model::NavigationModel::Field::Activity), 0);
    }
    if (!displayIsOn) turnDisplayOn();
}

void DisplayController::noteActivity() {
    resetActivityTime();
    if (currentMode == SLEEP) {
        const MatrixPageDescriptor* page = findMatrixPage(currentPageX, currentPageY);
        setMode(page ? page->mode : INFO);
    }
}

void DisplayController::onInput(const input::InputEvent& event) {
    noteActivity();
    if (currentMode == SLEEP || currentMode == INITIALIZATION) {
        return;
    }
    // WP2.3: the active screen sees every event first; the matrix navigation below only acts on
    // the pages that have no gestures of their own.
    switch (currentMode) {
        case INFO:        if (infoScreen) infoScreen->onInput(event); break;
        case DEVICE_INFO: if (deviceInfoScreen) deviceInfoScreen->onInput(event); break;
        case MATRIX_PAGE: if (placeholderScreen) placeholderScreen->onInput(event); break;
        case HOME:        if (modeController) modeController->onInput(event); break;
        case SOUND:       if (soundController) soundController->onInput(event); break;
        case LIGHT:       if (lightController) lightController->onInput(event); break;
        default: break;
    }
    switch (event.type) {
        case input::InputType::Gesture:
            if (currentMode != DEVICE_INFO) {
                handleMatrixNavigationGesture(event.gesture);
            }
            break;
        case input::InputType::KnobDelta:
            applyKnobDelta(event.knobDelta);
            break;
        default:
            break;
    }
}

void DisplayController::applyKnobDelta(int16_t delta) {
    if (delta == 0) {
        return;
    }
    const bool up = delta > 0;
    const int steps = up ? delta : -delta;
    for (int i = 0; i < steps; ++i) {
        switch (currentMode) {
            case SOUND:
                if (soundController) {
                    if (up) soundController->incrementSetpoint(); else soundController->decrementSetpoint();
                }
                break;
            case LIGHT:
                if (lightController) {
                    if (up) lightController->incrementSetpoint(); else lightController->decrementSetpoint();
                }
                break;
            case HOME:
                if (modeController) {
                    if (up) modeController->nextMode(); else modeController->previousMode();
                    postNavigation(static_cast<int>(model::NavigationModel::Field::ModeStep), up ? 1 : -1);
                }
                break;
            default:
                break;
        }
    }
}

void DisplayController::turnDisplayOff() {
    if (!displayIsOn) return;
#if defined(LCD_BL_PIN) && (LCD_BL_PIN != -1)
    const int currentBrightness = std::clamp((brightnessPercent * 255 + 50) / 100, 0, 255);
    for (int i = currentBrightness; i >= 0; i -= 5) {
        LvglDisplay::setBrightness(static_cast<uint8_t>(i));
        vTaskDelay(pdMS_TO_TICKS(1));
    }
#else
    LvglDisplay::setBrightness(0);
#endif
    displayIsOn = false;
    ESP_LOGI(TAG, "Display off");
}

void DisplayController::turnDisplayOn() {
    if (displayIsOn) return;
    const int targetBrightness = std::clamp((brightnessPercent * 255 + 50) / 100, 0, 255);
#if defined(LCD_BL_PIN) && (LCD_BL_PIN != -1)
    for (int i = 0; i <= targetBrightness; i += 5) {
        LvglDisplay::setBrightness(static_cast<uint8_t>(i));
        vTaskDelay(pdMS_TO_TICKS(1));
    }
    LvglDisplay::setBrightness(static_cast<uint8_t>(targetBrightness));
#else
    LvglDisplay::setBrightness(static_cast<uint8_t>(targetBrightness));
#endif
    displayIsOn = true;
    ESP_LOGI(TAG, "Display on (%d%%)", brightnessPercent);
}

void DisplayController::setBrightnessPercent(int percent) {
    brightnessPercent = std::clamp(percent, 0, 100);
    const int brightness = std::clamp((brightnessPercent * 255 + 50) / 100, 0, 255);

    if (displayIsOn) {
        LvglDisplay::setBrightness(static_cast<uint8_t>(brightness));
    }

    ESP_LOGI(TAG, "Display brightness set to %d%%", brightnessPercent);
}

int DisplayController::getBrightnessPercent() const {
    return brightnessPercent;
}

bool DisplayController::handleMatrixNavigationGesture(input::Gesture gesture) {
    if (currentMode != INFO && currentMode != DEVICE_INFO && currentMode != MATRIX_PAGE) {
        return false;
    }

    int targetX = currentPageX;
    int targetY = currentPageY;

    switch (gesture) {
        case input::Gesture::SwipeLeft:
            targetX -= 1;
            break;
        case input::Gesture::SwipeRight:
            targetX += 1;
            break;
        case input::Gesture::SwipeUp:
            targetY += 1;
            break;
        case input::Gesture::SwipeDown:
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

// ---- WP1.4 shadow-phase posts (INPT-05, MDL-31) --------------------------------------------

void DisplayController::registerModelQueue(model::MutationQueue* queue) {
    modelQueue = queue;
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
        case MATRIX_PAGE:           // placeholder pages ported from main; they become real screens
        case CALIBRATE_ORIENTATION: // in Phase 3, so the model keeps the last real one for now
        case SLEEP:
        default:
            return;  // D8: SLEEP is a power state, not a screen
    }
    postNavigation(static_cast<int>(model::NavigationModel::Field::ActiveScreen), static_cast<int32_t>(screen));
}
