/*
 * QNOB Screen Application - ESP-IDF Version
 *
 * Main entry point for the QNOB smart knob display application.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "driver/gpio.h"
#include <sstream>

// HAL and hardware components
#include "hal_config.h"
#include "hal_init.h"

// Display
#include "DisplayController.h"
#include "LvglDisplay.h"
#include "LGFX_Config.hpp"

// Hardware interfaces
#include "TouchPanel.h"
#include "KnobController.h"
#include "NVSManager.h"
#include "WiFiManager.h"
#include "MQTTManager.h"
#include "InternetHandler.h"

// Controllers
#include "CommandHandler.h"
#include "SoundController.h"
#include "LightController.h"
#include "MediaController.h"
#include "OTAManager.h"
#include "ModeController.h"
#include "InitializationScreen.h"
#include "EnvironmentInfoScreen.h"
#include "DeviceInfoScreen.h"
#include "PlaceholderPageScreen.h"
#include "BatteryHandler.h"
#include "BluetoothManager.h"

// Media
#include "SDCard.h"
#include "Speaker.h"
#include "Microphone.h"
#include "SoundPlayer.h"
#include "SoundRecorder.h"

// Power Management
#include "SleepHandler.h"


static const char *TAG = "QNOB_MAIN";
static constexpr const char* kDefaultOtaManifestBaseUrl = "https://alperbasarn.github.io/homio-round-hmi/ota/latest";

namespace {

BatteryHandler::Config getBatteryHandlerConfig() {
    BatteryHandler::Config config = {};

#if QNOB_BATTERY_SOURCE == QNOB_BATTERY_SOURCE_ADC
    config.source = BatteryHandler::TelemetrySource::ADC;
    config.adcChannel = BAT_ADC_PIN;
#elif QNOB_BATTERY_SOURCE == QNOB_BATTERY_SOURCE_AXP2101
    config.source = BatteryHandler::TelemetrySource::AXP2101;
    config.pmuI2cAddress = PMU_I2C_ADDR;
#else
    config.source = BatteryHandler::TelemetrySource::NONE;
#endif

    return config;
}

constexpr bool kAudioOutputSupported =
    (QNOB_AUDIO_OUTPUT_BACKEND != QNOB_AUDIO_OUTPUT_BACKEND_NONE);
constexpr bool kAudioInputSupported =
    (QNOB_AUDIO_INPUT_BACKEND != QNOB_AUDIO_INPUT_BACKEND_NONE);

}  // namespace

// Global objects
static LGFX* gfx = nullptr;
static TouchPanel* touchPanel = nullptr;
static KnobController* knobController = nullptr;
static NVSManager* nvsManager = nullptr;
static WiFiManager* wifiManager = nullptr;
static MQTTManager* mqttManager = nullptr;
static InternetHandler* internetHandler = nullptr;
static DisplayController* displayController = nullptr;
static CommandHandler* commandHandler = nullptr;
static SoundController* soundController = nullptr;
static LightController* lightController = nullptr;
static ModeController* modeController = nullptr;
static InitializationScreen* initializationScreen = nullptr;
static EnvironmentInfoScreen* infoScreen = nullptr;
static DeviceInfoScreen* deviceInfoScreen = nullptr;
static PlaceholderPageScreen* placeholderPageScreen = nullptr;
static BatteryHandler* batteryHandler = nullptr;
static BluetoothManager* bluetoothManager = nullptr;
static SDCard* sdCard = nullptr;
static Speaker* speaker = nullptr;
static Microphone* microphone = nullptr;
static SoundPlayer* soundPlayer = nullptr;
static SoundRecorder* soundRecorder = nullptr;
static MediaController* mediaController = nullptr;
static OTAManager* otaManager = nullptr;
static SleepHandler* sleepHandler = nullptr;

static std::string resolveManifestUrl(const std::string& variantId,
                                      const std::string& configuredManifestUrl) {
    if (configuredManifestUrl.empty()) {
        return std::string(kDefaultOtaManifestBaseUrl) + "/" + variantId + ".json";
    }

    const std::string placeholder = "{variant}";
    const size_t pos = configuredManifestUrl.find(placeholder);
    if (pos == std::string::npos) {
        return configuredManifestUrl;
    }

    std::string expanded = configuredManifestUrl;
    expanded.replace(pos, placeholder.size(), variantId);
    return expanded;
}

static void applyOtaConfigFromNvs() {
    if (otaManager == nullptr || nvsManager == nullptr) {
        return;
    }

    const std::string variantId = nvsManager->otaVariantId.empty()
        ? std::string(QNOB_OTA_VARIANT_ID)
        : nvsManager->otaVariantId;
    const std::string manifestUrl = resolveManifestUrl(variantId, nvsManager->otaManifestUrl);

    otaManager->setDeviceVariantId(variantId);
    otaManager->setManifestUrl(manifestUrl);
    ESP_LOGI(TAG, "OTA config applied: variant=%s, manifest=%s", variantId.c_str(), manifestUrl.c_str());
}

void bluetoothInitTask(void* parameter) {
    auto* manager = static_cast<BluetoothManager*>(parameter);
    if (manager != nullptr) {
        const std::string btName = (nvsManager != nullptr && !nvsManager->bluetoothName.empty())
            ? nvsManager->bluetoothName : "Qnob PC Control";
        const esp_err_t err = manager->begin(btName.c_str());
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Bluetooth init failed: %s", esp_err_to_name(err));
        }
    }
    vTaskDelete(nullptr);
}


// Task handles
static TaskHandle_t displayTaskHandle = nullptr;
static TaskHandle_t networkTaskHandle = nullptr;
static TaskHandle_t commandTaskHandle = nullptr;

// Main task for display and UI
void displayTask(void* parameter) {
    ESP_LOGI(TAG, "Display task started on core %d", xPortGetCoreID());

#if defined(PWR_KEY_PIN) && (PWR_KEY_PIN >= 0)
    // Configure PWR_KEY as input for software reset
    gpio_config_t pwr_cfg = {};
    pwr_cfg.intr_type = GPIO_INTR_DISABLE;
    pwr_cfg.mode = GPIO_MODE_INPUT;
    pwr_cfg.pin_bit_mask = (1ULL << PWR_KEY_PIN);
    pwr_cfg.pull_up_en = GPIO_PULLUP_ENABLE;
    pwr_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&pwr_cfg);
#endif

    while (true) {
#if defined(PWR_KEY_PIN) && (PWR_KEY_PIN >= 0)
        // PWR_KEY pressed (active LOW) → software reset
        if (gpio_get_level((gpio_num_t)PWR_KEY_PIN) == 0) {
            ESP_LOGI(TAG, "PWR_KEY pressed - restarting...");
            vTaskDelay(pdMS_TO_TICKS(50));  // debounce
            esp_restart();
        }
#endif

        if (batteryHandler && batteryHandler->isInitialized()) {
            batteryHandler->analyze();
        }
        displayController->update();

        // Check power management
        if (sleepHandler) {
            sleepHandler->checkActivity();
        }

        // Drive rendering at 50 FPS. LVGL keeps its own 5 ms tick for timing.
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void startDisplayTaskIfNeeded() {
    if (displayTaskHandle != nullptr) {
        return;
    }

#if DUAL_CORE_AVAILABLE
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle, DISPLAY_CORE);
#else
    xTaskCreate(displayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle);
#endif
}

// Main task for network-related activities
void networkTask(void* parameter) {
    ESP_LOGI(TAG, "Network task started on core %d", xPortGetCoreID());
    while (true) {
        wifiManager->update();
        if (wifiManager->isConnected()) {
            mqttManager->update();
            internetHandler->update();
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

// Task for handling commands
void commandTask(void* parameter) {
    ESP_LOGI(TAG, "Command task started on core %d", xPortGetCoreID());
    while (true) {
        commandHandler->update();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void startupSoundTask(void* parameter) {
    auto* player = static_cast<SoundPlayer*>(parameter);
    if (player != nullptr) {
        // Allow the initialization screen to render before audio starts.
        vTaskDelay(pdMS_TO_TICKS(50));
        player->playStartupSound();
    }
    vTaskDelete(nullptr);
}

extern "C" void app_main(void) {
    // FIRST: Reset display panel before anything else
    // This fixes white screen issue when serial monitor triggers soft reset
    // or when the panel is left in an undefined state
#if LCD_RST_PIN >= 0
    // Configure LCD RST pin as output (before HAL init, just for this pin)
    gpio_config_t rst_conf = {};
    rst_conf.intr_type = GPIO_INTR_DISABLE;
    rst_conf.mode = GPIO_MODE_OUTPUT;
    rst_conf.pin_bit_mask = (1ULL << LCD_RST_PIN);
    rst_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    rst_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&rst_conf);

    // Robust AMOLED reset sequence:
    // 1. Ensure reset is HIGH first (in case it was left LOW)
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 2. First reset pulse - brings panel to known state
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 1);
    vTaskDelay(pdMS_TO_TICKS(50));

    // 3. Second reset pulse - ensures clean init (some panels need this)
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level((gpio_num_t)LCD_RST_PIN, 1);

    // 4. Wait for panel to fully initialize (AMOLED needs longer than LCD)
    // SH8601Z datasheet recommends 120ms after reset before commands
    vTaskDelay(pdMS_TO_TICKS(150));
#endif

    ESP_LOGI(TAG, "===========================================");
    ESP_LOGI(TAG, "QNOB Screen Application Starting...");
    ESP_LOGI(TAG, "===========================================");

    // Initialize HAL
    ESP_LOGI(TAG, "Initializing HAL...");
    ESP_ERROR_CHECK(board_hal_init());

    // Initialize Battery Handler
    ESP_LOGI(TAG, "Initializing battery handler...");
    batteryHandler = new BatteryHandler();
    if (batteryHandler->initialize(getBatteryHandlerConfig()) != ESP_OK) {
        ESP_LOGW(TAG, "Battery handler init failed - battery monitoring disabled");
    }

    // Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    nvsManager = new NVSManager();
    ESP_ERROR_CHECK(nvsManager->begin());

    // Create display object early so the pointer is valid, but defer hardware init
    // until just before the display task starts (after WiFi init) to avoid a 7+
    // second idle gap between gfx->init() and the first actual draw call, which
    // causes the CO5300 AMOLED to drop subsequent pixel commands (blank screen).
    ESP_LOGI(TAG, "Initializing display...");
    gfx = new LGFX();

    // Initialize Touch Panel
    ESP_LOGI(TAG, "Initializing touch panel...");
    touchPanel = new TouchPanel(TP_SDA_PIN, TP_SCL_PIN, TP_RST_PIN, TP_INT_PIN);
    esp_err_t touch_err = touchPanel->initialize();
    if (touch_err != ESP_OK) {
        ESP_LOGE(TAG, "Touch panel init failed: %s", esp_err_to_name(touch_err));
    }

    // Initialize SD card (optional - media features disabled if not present)
    ESP_LOGI(TAG, "Initializing SD card...");
    sdCard = new SDCard();
    esp_err_t sd_err = sdCard->initialize();
    if (sd_err != ESP_OK || !sdCard->isCardPresent()) {
        ESP_LOGW(TAG, "SD card not available - media features disabled");
        delete sdCard;
        sdCard = nullptr;
    }

    if (sdCard != nullptr) {
        ESP_LOGI(TAG, "Media folders:");
        ESP_LOGI(TAG, "  %s", sdCard->getPicturesDir().c_str());
        ESP_LOGI(TAG, "  %s", sdCard->getModeLogosDir().c_str());
        ESP_LOGI(TAG, "  %s", sdCard->getInitAnimationsDir().c_str());
        ESP_LOGI(TAG, "  %s", sdCard->getSoundEffectsDir().c_str());
        ESP_LOGI(TAG, "  %s", sdCard->getReactionSoundsDir().c_str());
        ESP_LOGI(TAG, "  %s", sdCard->getRecordingsDir().c_str());
    }

    bool startupChimeReady = false;

    // Initialize audio/media classes only when the selected board exposes audio.
    if (!kAudioOutputSupported && !kAudioInputSupported) {
        ESP_LOGI(TAG, "Audio is not supported on %s - media features disabled", QNOB_BOARD_NAME);
    } else {
        ESP_LOGI(TAG, "Initializing audio/media classes...");

        if (kAudioOutputSupported) {
            speaker = new Speaker();
            soundPlayer = new SoundPlayer(sdCard, speaker);
            if (soundPlayer->initialize() != ESP_OK) {
                ESP_LOGW(TAG, "SoundPlayer initialization failed");
            } else {
                startupChimeReady = true;
            }
        } else {
            ESP_LOGI(TAG, "Audio output disabled for %s", QNOB_BOARD_NAME);
        }

        if (kAudioInputSupported) {
            microphone = new Microphone();
            soundRecorder = new SoundRecorder(sdCard, microphone);
            if (soundRecorder->initialize() != ESP_OK) {
                ESP_LOGW(TAG, "SoundRecorder initialization failed");
            }
        } else {
            ESP_LOGI(TAG, "Audio input disabled for %s", QNOB_BOARD_NAME);
        }

        if (soundPlayer != nullptr) {
            mediaController = new MediaController(soundRecorder, soundPlayer);
            if (mediaController->initialize() != ESP_OK) {
                ESP_LOGW(TAG, "MediaController initialization failed");
            }
        } else if (soundRecorder != nullptr) {
            ESP_LOGW(TAG, "Audio input is present but no playback path is configured; media controller disabled");
        }
    }

    // Initialize Knob Controller
#if (KNOB_TX_PIN >= 0) && (KNOB_RX_PIN >= 0)
    ESP_LOGI(TAG, "Initializing knob controller...");
    knobController = new KnobController(KNOB_TX_PIN, KNOB_RX_PIN, KNOB_UART_NUM);
    if (knobController->begin(KNOB_BAUD_RATE) != ESP_OK) {
        ESP_LOGW(TAG, "Knob controller init failed - continuing without UART knob");
        delete knobController;
        knobController = nullptr;
    }
#else
    ESP_LOGI(TAG, "Knob UART is not mapped for %s - skipping knob controller init", QNOB_BOARD_NAME);
#endif

    // Initialize networking
    ESP_LOGI(TAG, "Initializing networking...");
    wifiManager = new WiFiManager(nvsManager);
    wifiManager->initialize();
    mqttManager = new MQTTManager(wifiManager, nvsManager);
    if (mqttManager->initialize() != ESP_OK) {
        ESP_LOGW(TAG, "MQTT not configured yet (init skipped)");
    }
    internetHandler = new InternetHandler(wifiManager, nvsManager);
    otaManager = new OTAManager(wifiManager);
    applyOtaConfigFromNvs();
    wifiManager->setSetupPortalOtaConfigUpdatedCallback([]() {
        applyOtaConfigFromNvs();
    });
    wifiManager->setSetupPortalOtaStatusCallback([]() -> std::string {
        if (otaManager == nullptr) {
            return "{\"configured\":false,\"busy\":false,\"update_available\":false,\"current_version\":\"unknown\",\"available_version\":\"\",\"status_message\":\"OTA manager unavailable\"}";
        }

        const OtaReleaseInfo info = otaManager->getReleaseInfo();
        auto escape = [](const std::string& value) {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                if (c == '\\' || c == '"') {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            return out;
        };

        std::ostringstream os;
        os << "{";
        os << "\"configured\":" << (info.configured ? "true" : "false") << ",";
        os << "\"busy\":" << (info.busy ? "true" : "false") << ",";
        os << "\"update_available\":" << (info.updateAvailable ? "true" : "false") << ",";
        os << "\"current_version\":\"" << escape(info.currentVersion) << "\",";
        os << "\"available_version\":\"" << escape(info.availableVersion) << "\",";
        os << "\"status_message\":\"" << escape(info.statusMessage) << "\"";
        os << "}";
        return os.str();
    });
    wifiManager->setSetupPortalOtaActionCallback([](const std::string& action) -> esp_err_t {
        if (otaManager == nullptr) {
            return ESP_ERR_INVALID_STATE;
        }

        if (action == "check") {
            return otaManager->checkForReleaseUpdate();
        }
        if (action == "update") {
            return otaManager->startReleaseUpdate(true);
        }
        return ESP_ERR_INVALID_ARG;
    });
    wifiManager->setSetupPortalDeviceInfoStatusCallback([]() -> std::string {
        auto escape = [](const std::string& value) {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                if (c == '\\' || c == '"') {
                    out.push_back('\\');
                }
                out.push_back(c);
            }
            return out;
        };

        bool wifi = wifiManager != nullptr && wifiManager->isConnected();
        bool internet = internetHandler != nullptr && internetHandler->isInternetAvailable();
        bool mqtt = mqttManager != nullptr && mqttManager->isConnected();
        bool bluetoothEnabled = bluetoothManager != nullptr && bluetoothManager->isEnabled();
        bool bluetoothReady = bluetoothManager != nullptr && bluetoothManager->isReady();
        bool bluetoothConnected = bluetoothManager != nullptr && bluetoothManager->isConnected();
        bool bluetoothHidConnected = bluetoothManager != nullptr && bluetoothManager->isHidConnected();
        bool bluetoothSerialConnected = bluetoothManager != nullptr && bluetoothManager->isSerialConnected();
        int strength = wifiManager != nullptr ? wifiManager->getSignalStrength() : 0;

        bool batteryPresenceKnown = false;
        bool batteryConnected = false;
        bool batteryPercentageAvailable = false;
        float batteryPercentage = -1.0f;
        float batteryVoltage = -1.0f;
        if (batteryHandler != nullptr && batteryHandler->isInitialized()) {
            const BatteryHandler::BatteryTelemetry telemetry = batteryHandler->getBatteryTelemetry();
            batteryConnected = batteryHandler->isBatteryConnected();
            batteryPercentage = telemetry.percentage;
            batteryPercentageAvailable = telemetry.percentage >= 0.0f;
            batteryVoltage = telemetry.voltageVolts;
        }

        bool softwareConfigured = false;
        bool softwareBusy = false;
        bool softwareUpdateAvailable = false;
        std::string currentVersion = "unknown";
        std::string availableVersion;
        std::string statusText = "OTA unavailable";
        if (otaManager != nullptr) {
            const OtaReleaseInfo info = otaManager->getReleaseInfo();
            softwareConfigured = info.configured;
            softwareBusy = info.busy;
            softwareUpdateAvailable = info.updateAvailable;
            currentVersion = info.currentVersion;
            availableVersion = info.availableVersion;
            statusText = info.statusMessage;
        }

        std::ostringstream os;
        os << "{";
        os << "\"wifi_connected\":" << (wifi ? "true" : "false") << ",";
        os << "\"internet_connected\":" << (internet ? "true" : "false") << ",";
        os << "\"mqtt_connected\":" << (mqtt ? "true" : "false") << ",";
        const std::string btName = (nvsManager != nullptr) ? nvsManager->bluetoothName : "Qnob PC Control";
        os << "\"bluetooth_name\":\"" << escape(btName) << "\",";
        os << "\"bluetooth_enabled\":" << (bluetoothEnabled ? "true" : "false") << ",";
        os << "\"bluetooth_ready\":" << (bluetoothReady ? "true" : "false") << ",";
        os << "\"bluetooth_connected\":" << (bluetoothConnected ? "true" : "false") << ",";
        os << "\"bluetooth_hid_connected\":" << (bluetoothHidConnected ? "true" : "false") << ",";
        os << "\"bluetooth_serial_connected\":" << (bluetoothSerialConnected ? "true" : "false") << ",";
        os << "\"wifi_strength_bars\":" << strength << ",";
        os << "\"battery_presence_known\":" << (batteryPresenceKnown ? "true" : "false") << ",";
        os << "\"battery_connected\":" << (batteryConnected ? "true" : "false") << ",";
        os << "\"battery_percentage_available\":" << (batteryPercentageAvailable ? "true" : "false") << ",";
        os << "\"battery_percentage\":" << batteryPercentage << ",";
        os << "\"battery_voltage\":" << batteryVoltage << ",";
        os << "\"software_configured\":" << (softwareConfigured ? "true" : "false") << ",";
        os << "\"software_busy\":" << (softwareBusy ? "true" : "false") << ",";
        os << "\"software_update_available\":" << (softwareUpdateAvailable ? "true" : "false") << ",";
        os << "\"current_version\":\"" << escape(currentVersion) << "\",";
        os << "\"available_version\":\"" << escape(availableVersion) << "\",";
        os << "\"status_text\":\"" << escape(statusText) << "\"";
        os << "}";
        return os.str();
    });

    // Initialize UI controllers
    ESP_LOGI(TAG, "Initializing UI controllers...");
    bluetoothManager = &BluetoothManager::instance();
    modeController = new ModeController(touchPanel);
    initializationScreen = new InitializationScreen();

    // Create EnvironmentInfoScreen (time, weather, temperature)
    infoScreen = new EnvironmentInfoScreen(touchPanel);
    infoScreen->setDateTimeCallback([](std::string& date, std::string& time, std::string& dayOfWeek) {
        date = internetHandler->getCurrentDate();
        time = internetHandler->getCurrentTime();
        dayOfWeek = internetHandler->getDayOfWeek();
    });
    infoScreen->setOutdoorTempCallback([]() {
        return internetHandler->getOutdoorTemperature();
    });

    // Create DeviceInfoScreen (WiFi, internet, MQTT, battery)
    deviceInfoScreen = new DeviceInfoScreen(touchPanel);
    deviceInfoScreen->setNetworkStatusCallback([](bool& wifi, bool& internet, bool& mqtt, int& strength) {
        wifi = wifiManager->isConnected();
        internet = internetHandler->isInternetAvailable();
        mqtt = mqttManager->isConnected();
        strength = wifiManager->getSignalStrength();
    });
    deviceInfoScreen->setNetworkDetailsCallback([](std::string& ssid, std::string& ip) {
        ssid = wifiManager != nullptr ? wifiManager->getSSID() : "";
        ip = wifiManager != nullptr ? wifiManager->getIPAddress() : "";
    });
    deviceInfoScreen->setBluetoothStatusCallback([]() {
        return bluetoothManager != nullptr && bluetoothManager->isConnected();
    });
    deviceInfoScreen->setBatteryCallback([]() -> DeviceBatteryStatus {
        if (batteryHandler && batteryHandler->isInitialized()) {
            const BatteryHandler::BatteryTelemetry telemetry = batteryHandler->getBatteryTelemetry();
            const bool percentageAvailable = telemetry.percentage >= 0.0f;
            return {false, batteryHandler->isBatteryConnected(), percentageAvailable,
                    telemetry.percentage, telemetry.voltageVolts};
        }
        return {false, false, false, -1.0f, -1.0f};
    });
    deviceInfoScreen->setSoftwareUpdateStatusCallback([]() -> DeviceSoftwareUpdateState {
        if (otaManager == nullptr) {
            return {false, false, false, "unknown", "", "OTA manager unavailable"};
        }

        const OtaReleaseInfo info = otaManager->getReleaseInfo();
        return {info.configured, info.busy, info.updateAvailable,
                info.currentVersion, info.availableVersion, info.statusMessage};
    });
    deviceInfoScreen->setSoftwareUpdateActionCallback([]() {
        if (otaManager == nullptr) {
            return;
        }

        const esp_err_t err = otaManager->startReleaseUpdate(true);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start release OTA update: %s", esp_err_to_name(err));
        }
    });

    placeholderPageScreen = new PlaceholderPageScreen(touchPanel);
    placeholderPageScreen->setPlayPauseAction([]() {
        if (bluetoothManager != nullptr) {
            bluetoothManager->sendPlayPause();
        }
    });

    soundController = new SoundController(touchPanel);
    lightController = new LightController(touchPanel);
    displayController = new DisplayController(touchPanel);
    displayController->registerModeController(modeController);
    displayController->registerInitializationScreen(initializationScreen);
    displayController->registerInfoScreen(infoScreen);
    displayController->registerDeviceInfoScreen(deviceInfoScreen);
    displayController->registerPlaceholderScreen(placeholderPageScreen);
    displayController->registerMediaController(mediaController);
    displayController->registerSoundController(soundController);
    displayController->registerLightController(lightController);
    displayController->registerKnobController(knobController);
    deviceInfoScreen->setBrightnessControlCallbacks(
        []() {
            return displayController != nullptr ? displayController->getBrightnessPercent() : 100;
        },
        [](int percent) {
            if (displayController != nullptr) {
                displayController->setBrightnessPercent(percent);
            }
        });
    deviceInfoScreen->setSoundControlCallbacks(
        []() {
            return soundController != nullptr ? soundController->getSetpoint() : 50;
        },
        [](int percent) {
            if (soundController != nullptr) {
                soundController->updateSetpoint(percent);
            }
        });
    wifiManager->setSetupPortalScreenControlCallback([](const std::string& screen) {
        if (commandHandler != nullptr) {
            commandHandler->handleExternalCommand("screen:" + screen);
            return true;
        }
        return displayController != nullptr && displayController->showNamedScreen(screen);
    });
    wifiManager->setSetupPortalScreenStatusCallback([]() -> std::string {
        return displayController != nullptr ? displayController->getModeName() : "unknown";
    });
    wifiManager->setSetupPortalCommandCallback([](const std::string& cmd) {
        if (commandHandler != nullptr) commandHandler->handleExternalCommand(cmd);
    });

    // Initialize display hardware here — immediately before the display task starts
    // so the gap between gfx->init() and the first draw is <100 ms.
    gfx->init();
    gfx->setRotation(0);
    // Provide the hardware driver to LvglDisplay before any draw calls.
    LvglDisplay::setHardware(static_cast<void*>(gfx));
    gfx->setBrightness(0);
    gfx->fillScreen(TFT_BLACK);
    vTaskDelay(pdMS_TO_TICKS(50));
    for (int i = 0; i <= 255; i += 15) {
        gfx->setBrightness(i);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    gfx->setBrightness(255);

    displayController->init();
    startDisplayTaskIfNeeded();
    wifiManager->connectToWiFi();

    // Initialize Sleep Handler (power management)
    ESP_LOGI(TAG, "Initializing sleep handler...");
    sleepHandler = SleepHandler::getInstance();
    if (sleepHandler->initialize() == ESP_OK) {
        // Set power save mode callback (screen off)
        sleepHandler->setPowerSaveModeCallback([](bool enabled) {
            if (enabled) {
                ESP_LOGI(TAG, "Power save mode: Turning display off");
                displayController->turnDisplayOff();
            } else {
                ESP_LOGI(TAG, "Power save mode: Turning display on");
                displayController->turnDisplayOn();
            }
        });

        // Set pre-sleep callback (before entering deep sleep)
        sleepHandler->setPreSleepCallback([]() {
            ESP_LOGI(TAG, "Preparing for deep sleep...");
            // Configure touch IC for wake-on-touch before I2C goes away
            if (touchPanel) {
                touchPanel->prepareForSleep();
            }
            // Turn off display
            if (displayController) {
                displayController->turnDisplayOff();
            }
            // Give time for display to turn off
            vTaskDelay(pdMS_TO_TICKS(100));
        });

        // Enable the sleep handler
        sleepHandler->enable();
        ESP_LOGI(TAG, "Sleep handler enabled with 4-stage power management");
    } else {
        ESP_LOGW(TAG, "Sleep handler initialization failed");
    }

    // Initialize Command Handler
    ESP_LOGI(TAG, "Initializing command handler...");
    commandHandler = new CommandHandler(displayController, nvsManager, wifiManager, mqttManager);
    commandHandler->registerKnobController(knobController);
    commandHandler->registerMediaController(mediaController);
    commandHandler->registerOTAManager(otaManager);
    commandHandler->registerBluetoothManager(bluetoothManager);
    commandHandler->setOtaConfigUpdatedCallback([]() {
        applyOtaConfigFromNvs();
    });
    commandHandler->begin();

    // Set MQTT callbacks for sound controller
    soundController->setMQTTPublishCallback([&](const std::string& topic, const std::string& message) {
        mqttManager->publish(topic, message);
    });
    soundController->setMQTTConnectedCallback([&]() {
        return mqttManager->isConnected();
    });

    // Set MQTT callbacks for light controller
    lightController->setMQTTPublishCallback([&](const std::string& topic, const std::string& message) {
        mqttManager->publish(topic, message);
    });
    lightController->setMQTTConnectedCallback([&]() {
        return mqttManager->isConnected();
    });
    lightController->setMQTTConfiguredCallback([&]() {
        return mqttManager->hasLightMQTTConfigured();
    });

    // Set global MQTT message callback
    mqttManager->setMessageCallback([&](const std::string& topic, const std::string& payload) {
        if (topic == MQTT_TOPIC_COMMAND) {
            if (commandHandler) {
                commandHandler->handleExternalCommand(payload);
            }
        } else if (topic == MQTT_TOPIC_INDOOR_TEMP) {
            // Indoor temperature from bridge (e.g., Home Assistant → MQTT)
            try {
                float indoorTemp = std::stof(payload);
                if (infoScreen) {
                    infoScreen->updateIndoorTemperature(indoorTemp);
                }
            } catch (...) {
                ESP_LOGW(TAG, "Invalid indoor temperature payload: %s", payload.c_str());
            }
        } else if (topic.find("sound") != std::string::npos) {
            soundController->onMQTTMessage(payload);
        } else if (topic.find("light") != std::string::npos && topic.find("brightness") != std::string::npos) {
            try {
                int brightness = std::stoi(payload);
                lightController->onBrightnessReceived(brightness);
            } catch (...) {}
        }
    });

    ESP_LOGI(TAG, "Initialization complete. Starting tasks...");

    // Start tasks
    startDisplayTaskIfNeeded();
#if DUAL_CORE_AVAILABLE
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle, NETWORK_CORE);
    xTaskCreatePinnedToCore(commandTask, "CommandTask", 4096, nullptr, 5, &commandTaskHandle, NETWORK_CORE);
    xTaskCreatePinnedToCore(bluetoothInitTask, "BluetoothInit", 8192, bluetoothManager, 3, nullptr, NETWORK_CORE);
#else
    xTaskCreate(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle);
    xTaskCreate(commandTask, "CommandTask", 4096, nullptr, 5, &commandTaskHandle);
    xTaskCreate(bluetoothInitTask, "BluetoothInit", 8192, bluetoothManager, 3, nullptr);
#endif

    if (startupChimeReady) {
        // Startup chime with 16384 byte stack to handle audio codec operations
        xTaskCreate(startupSoundTask, "StartupSound", 16384, soundPlayer, 2, nullptr);
    }

    ESP_LOGI(TAG, "Tasks started.");
}
