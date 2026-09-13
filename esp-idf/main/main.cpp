/*
 * QNOB Screen Application - ESP-IDF Version
 *
 * Main entry point for the QNOB smart knob display application.
 *
 * WP1.4: the model root lives here (APP-06). Every existing data path also posts mutation
 * records into it (MDL-31), the pollers on the network task feed it (NET-06, NET-07, PERI-06),
 * the display task drains it at the top of each frame, and the four screen callbacks read the
 * model instead of the drivers (NET-11). The screens themselves are unchanged until Phase 3.
 */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "driver/gpio.h"

// HAL and hardware components
#include "hal_config.h"
#include "hal_init.h"

// Display
#include "DisplayBackendFactory.h"
#include "DisplayPanelBackend.h"
#include "DisplayController.h"
#include "LvglDisplay.h"

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
#include "LocalAudio.h"
#include "OTAManager.h"
#include "ModeController.h"
#include "InitializationScreen.h"
#include "EnvironmentInfoScreen.h"
#include "DeviceInfoScreen.h"
#include "PlaceholderPageScreen.h"
#include "esp_app_desc.h"
#include "BatteryHandler.h"

// Media
#include "SDCard.h"
#include "Speaker.h"
#include "Microphone.h"
#include "SoundPlayer.h"
#include "SoundRecorder.h"

// Power Management
#include "SleepHandler.h"

// Model and adapters
#include "adapters/BatteryPoller.h"
#include "adapters/ConnectivityPoller.h"
#include "adapters/EnvironmentPoller.h"
#include "adapters/ModelHost.h"
#include "model/AppModel.h"

#include <memory>
#include <string>


static const char *TAG = "QNOB_MAIN";

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

int64_t nowMs() {
    return esp_timer_get_time() / 1000;
}

}  // namespace

// The model root (APP-06): constructed before anything else in this file, held by value.
static model::AppModel appModel;

// Global objects
static std::unique_ptr<DisplayPanelBackend> displayPanelBackend;
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
static SDCard* sdCard = nullptr;
static Speaker* speaker = nullptr;
static Microphone* microphone = nullptr;
static SoundPlayer* soundPlayer = nullptr;
static SoundRecorder* soundRecorder = nullptr;
static LocalAudio* localAudio = nullptr;
static OTAManager* otaManager = nullptr;
static SleepHandler* sleepHandler = nullptr;

// Pollers (network task) that push driver state into the model
static adapters::ConnectivityPoller* connectivityPoller = nullptr;
static adapters::EnvironmentPoller* environmentPoller = nullptr;
static adapters::BatteryPoller* batteryPoller = nullptr;


// Task handles
static TaskHandle_t displayTaskHandle = nullptr;
static TaskHandle_t networkTaskHandle = nullptr;
static TaskHandle_t commandTaskHandle = nullptr;
static TaskHandle_t startupSplashTaskHandle = nullptr;
static volatile bool startupSplashActive = false;
static constexpr uint32_t STARTUP_SOUND_TASK_STACK_SIZE = 8192;
static constexpr uint32_t STARTUP_SPLASH_TASK_STACK_SIZE = 4096;

static void startupSplashTask(void* parameter) {
    auto* screen = static_cast<InitializationScreen*>(parameter);
    const int64_t startTimeMs = esp_timer_get_time() / 1000;
    int lastProgress = -1;

    while (startupSplashActive && screen != nullptr) {
        const int64_t elapsedMs = (esp_timer_get_time() / 1000) - startTimeMs;
        int progress = 1 + static_cast<int>((elapsedMs * 84) / 10000);
        if (progress > 85) {
            progress = 85;
        }

        if (progress != lastProgress) {
            screen->setProgress(progress);
            lastProgress = progress;
        } else {
            screen->updateScreen();
        }

        vTaskDelay(pdMS_TO_TICKS(16));
    }

    vTaskDelete(nullptr);
}

// Main task for display and UI: the single consumer of the mutation queue and the only writer
// of the models (D1).
void displayTask(void* parameter) {
    adapters::model_host::adoptCurrentTaskAsConsumer();
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

        // D1: drain once at the top of the frame, before anything reads the models.
        appModel.drainPendingMutations(nowMs());

        displayController->update();

        // Check power management
        if (sleepHandler) {
            sleepHandler->checkActivity();
        }

        vTaskDelay(pdMS_TO_TICKS(16)); // ~60 FPS
    }
}

// Main task for network-related activities. Nothing in this loop blocks (NET-30), so the
// pollers keep their 100 ms cadence.
void networkTask(void* parameter) {
    ESP_LOGI(TAG, "Network task started on core %d", xPortGetCoreID());
    while (true) {
        const int64_t now = nowMs();

        // PERI-07: battery acquisition lives here, off the render loop, and runs with WiFi down.
        if (batteryHandler && batteryHandler->isInitialized()) {
            batteryHandler->analyze();
        }
        if (batteryPoller) {
            batteryPoller->poll(now);
        }

        wifiManager->update();
        internetHandler->update();   // owns "internet available"; also services time and weather
        mqttManager->update();

        if (connectivityPoller) {
            connectivityPoller->poll(now);
        }
        if (environmentPoller) {
            environmentPoller->poll(now);
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

    // APP-06: the model root's platform hooks, before any producer exists.
    adapters::model_host::install(appModel);
    model::MutationQueue& modelQueue = appModel.mutations();

    // Initialize Battery Handler
    ESP_LOGI(TAG, "Initializing battery handler...");
    batteryHandler = new BatteryHandler();
    if (batteryHandler->initialize(getBatteryHandlerConfig()) != ESP_OK) {
        ESP_LOGW(TAG, "Battery handler init failed - battery monitoring disabled");
    }
    batteryPoller = new adapters::BatteryPoller(batteryHandler, modelQueue);

    // Initialize NVS
    ESP_LOGI(TAG, "Initializing NVS...");
    nvsManager = new NVSManager();
    ESP_ERROR_CHECK(nvsManager->begin());

    // Initialize Display
    ESP_LOGI(TAG, "Initializing display...");

    displayPanelBackend = DisplayBackendFactory::createPanelBackend();
    if (!displayPanelBackend) {
        ESP_LOGE(TAG, "Display panel backend creation failed");
        return;
    }
    if (!displayPanelBackend->initialize()) {
        ESP_LOGE(TAG, "Display panel backend initialization failed");
        return;
    }

    if (!LvglDisplay::init(displayPanelBackend->flushBackend())) {
        ESP_LOGE(TAG, "LVGL display initialization failed");
        return;
    }
    LvglDisplay::setPanelBackend(displayPanelBackend.get());   // the ported screens set brightness through LvglDisplay
    displayPanelBackend->setBrightness(255);

    // Render the splash screen before slower peripheral/network setup starts.
    // The DisplayController takes over later, but this makes power-up visibly alive.
    initializationScreen = new InitializationScreen();
    initializationScreen->reset();
    initializationScreen->updateScreen();
    initializationScreen->setProgress(1);
    startupSplashActive = true;
    xTaskCreate(startupSplashTask,
                "StartupSplash",
                STARTUP_SPLASH_TASK_STACK_SIZE,
                initializationScreen,
                DISPLAY_TASK_PRIORITY,
                &startupSplashTaskHandle);
    ESP_LOGI(TAG, "Initialization screen rendered");

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
            localAudio = new LocalAudio(soundRecorder, soundPlayer);
            if (localAudio->initialize() != ESP_OK) {
                ESP_LOGW(TAG, "LocalAudio initialization failed");
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
    internetHandler = new InternetHandler(wifiManager);
    // NET-09: one owner of "internet available"; MQTT asks it through a predicate.
    mqttManager->setInternetPredicate([]() {
        return internetHandler != nullptr && internetHandler->isInternetAvailable();
    });
    wifiManager->connectToWiFi();   // NET-30: a request; the network task runs the sequence
    otaManager = new OTAManager(wifiManager);

    connectivityPoller = new adapters::ConnectivityPoller(wifiManager, internetHandler, mqttManager, modelQueue);
    environmentPoller = new adapters::EnvironmentPoller(internetHandler->getTimeHandler(),
                                                        internetHandler->getWeatherHandler(), modelQueue);

    // Initialize UI controllers
    ESP_LOGI(TAG, "Initializing UI controllers...");
    modeController = new ModeController(touchPanel);
    if (initializationScreen == nullptr) {
        initializationScreen = new InitializationScreen();
    }

    // Create EnvironmentInfoScreen (time, weather, temperature). NET-11: the callbacks read the
    // model, which the network-task pollers fill; the screen still owns its own fields.
    infoScreen = new EnvironmentInfoScreen(touchPanel);
    infoScreen->setDateTimeCallback([](std::string& date, std::string& time, std::string& dayOfWeek) {
        const model::EnvironmentState& e = appModel.environment().state();
        date = e.date.c_str();
        time = e.time.c_str();
        dayOfWeek = e.day.c_str();
    });
    infoScreen->setOutdoorTempCallback([]() {
        const model::EnvironmentState& e = appModel.environment().state();
        return e.outdoorValid ? e.outdoorTemp : 0.0f;   // the old "no reading" default
    });

    // Create DeviceInfoScreen (WiFi, internet, MQTT, battery)
    deviceInfoScreen = new DeviceInfoScreen(touchPanel);
    deviceInfoScreen->setNetworkStatusCallback([](bool& wifi, bool& internet, bool& mqtt, int& strength) {
        const model::ConnectivityState& c = appModel.connectivity().state();
        wifi = c.wifiConnected;
        internet = c.internetAvailable;
        mqtt = c.mqttConnected;
        strength = c.bars;
    });
    deviceInfoScreen->setBatteryCallback([]() -> DeviceBatteryStatus {
        const model::PowerState& p = appModel.power().state();
        return {p.presenceKnown, p.connected, p.percentValid, p.percent, p.voltage};
    });
    // Ported with the main DeviceInfoScreen (2026-09-12): network details, Bluetooth, software
    // update and the two control rows. Bluetooth and release updates are not in this tree yet.
    deviceInfoScreen->setNetworkDetailsCallback([](std::string& ssid, std::string& ip) {
        ssid = wifiManager != nullptr ? wifiManager->getSSID() : "";
        ip = wifiManager != nullptr ? wifiManager->getIPAddress() : "";
    });
    deviceInfoScreen->setBluetoothStatusCallback([]() { return false; });
    deviceInfoScreen->setSoftwareUpdateStatusCallback([]() -> DeviceSoftwareUpdateState {
        const esp_app_desc_t* app = esp_app_get_description();
        return {false, false, false, app != nullptr ? app->version : "unknown", "", "Release updates not wired"};
    });
    deviceInfoScreen->setSoftwareUpdateActionCallback([]() {
        ESP_LOGW(TAG, "Software update requested from the page, but release updates are not wired in this tree");
    });

    // Matrix placeholder pages (timer, light, sound, temperature, PC); the PC page play/pause goes
    // to the PC over the existing sound path.
    placeholderPageScreen = new PlaceholderPageScreen(touchPanel);
    placeholderPageScreen->setPlayPauseAction([]() {
        if (soundController != nullptr) {
            soundController->togglePlayPause();
        }
    });

    soundController = new SoundController(displayPanelBackend.get(), touchPanel);
    soundController->setModelQueue(&modelQueue);                       // CTRL-07
    lightController = new LightController(displayPanelBackend.get(), touchPanel);
    lightController->setModelQueue(&modelQueue);                       // CTRL-08
    displayController = new DisplayController(touchPanel);
    displayController->registerModelQueue(&modelQueue);                // INPT-05 + navigation shadow
    displayController->registerModeController(modeController);
    displayController->registerInitializationScreen(initializationScreen);
    displayController->registerInfoScreen(infoScreen);
    displayController->registerDeviceInfoScreen(deviceInfoScreen);
    displayController->registerPlaceholderScreen(placeholderPageScreen);
    displayController->registerLocalAudio(localAudio);
    displayController->registerSoundController(soundController);
    displayController->registerLightController(lightController);
    displayController->registerKnobController(knobController);
    deviceInfoScreen->setBrightnessControlCallbacks(
        []() { return displayController != nullptr ? displayController->getBrightnessPercent() : 100; },
        [](int percent) {
            if (displayController != nullptr) displayController->setBrightnessPercent(percent);
        });
    deviceInfoScreen->setSoundControlCallbacks(
        []() { return soundController != nullptr ? soundController->getSetpoint() : 50; },
        [](int percent) {
            if (soundController != nullptr) soundController->updateSetpoint(percent);
        });
    startupSplashActive = false;
    if (startupSplashTaskHandle != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(30));
        startupSplashTaskHandle = nullptr;
    }
    displayController->init();

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
    commandHandler->registerLocalAudio(localAudio);
    commandHandler->registerOTAManager(otaManager);
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
        // CTRL-03: lightMQTTServerURL has no writer anywhere, so this gate was permanently closed
        // and LIGHT never published. There is one MQTT client; the light topics go through it.
        return mqttManager->hasLightMQTTConfigured() || mqttManager->hasSoundMQTTConfigured();
    });

    // Set global MQTT message callback (runs on the MQTT task). MDL-31: every value that reaches
    // a screen from here also reaches the model as a record; nothing writes a model directly.
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
                const uint32_t atMs = static_cast<uint32_t>(nowMs());
                appModel.mutations().post(model::EnvironmentModel::makeFloat(model::EnvironmentModel::Field::IndoorTemp, indoorTemp, atMs));
                appModel.mutations().post(model::ClimateModel::make(model::ClimateModel::Field::IndoorSampled, 0, atMs));
            } catch (...) {
                ESP_LOGW(TAG, "Invalid indoor temperature payload: %s", payload.c_str());
            }
        } else if (topic.find("sound") != std::string::npos) {
            soundController->onMQTTMessage(payload);
        } else if (topic.find("light") != std::string::npos && topic.find("brightness") != std::string::npos) {
            try {
                int brightness = std::stoi(payload);
                lightController->onBrightnessReceived(brightness);
                appModel.mutations().post(model::LightModel::make(model::LightModel::Field::Brightness, brightness,
                                                        static_cast<uint32_t>(nowMs())));
            } catch (...) {}
        }
    });

    // MDL-30: no subscribers yet (the trace logs from inside the drain); sealing now guarantees
    // nothing re-wires in the render path.
    appModel.wireSubscriptions();

    ESP_LOGI(TAG, "Initialization complete. Starting tasks...");

    // Start tasks
#if DUAL_CORE_AVAILABLE
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle, DISPLAY_CORE);
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle, NETWORK_CORE);
    xTaskCreatePinnedToCore(commandTask, "CommandTask", COMMAND_STACK_SIZE, nullptr, COMMAND_TASK_PRIORITY, &commandTaskHandle, NETWORK_CORE);
#else
    xTaskCreate(displayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle);
    xTaskCreate(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle);
    xTaskCreate(commandTask, "CommandTask", COMMAND_STACK_SIZE, nullptr, COMMAND_TASK_PRIORITY, &commandTaskHandle);
#endif

    if (startupChimeReady) {
        xTaskCreate(startupSoundTask, "StartupSound", STARTUP_SOUND_TASK_STACK_SIZE, soundPlayer, 2, nullptr);
    }

    ESP_LOGI(TAG, "Tasks started.");
}
