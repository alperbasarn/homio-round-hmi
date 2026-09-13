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
#include "esp_attr.h"
#if CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"
#endif
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
#include "LightScreenController.h"
#include "LightView.h"
#include "LocalAudio.h"
#include "input/InputRouter.h"
#include "input/InputSfxListener.h"
#include "OTAManager.h"
#include "ModeController.h"
#include "BootScreenController.h"
#include "InitializationView.h"
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
#include "adapters/MqttModelBridge.h"
#include "model/AppModel.h"

#include <atomic>
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
static ui::LightView* lightView = nullptr;                          // LED-05
static controllers::LightScreenController* lightScreen = nullptr;  // LED-05
static ModeController* modeController = nullptr;
static ui::InitializationView* initializationView = nullptr;            // SCRN-07
static controllers::BootScreenController* bootController = nullptr;    // SCRN-08
// SCRN-09: the display task starts right after LVGL init so the boot arc animates through the
// peripheral and network setup; everything it may touch later (router, sleep handler, screens)
// is published through this flag once app_main has built and registered it.
static std::atomic<bool> s_compositionComplete{false};
static EnvironmentInfoScreen* infoScreen = nullptr;
static DeviceInfoScreen* deviceInfoScreen = nullptr;
static PlaceholderPageScreen* placeholderPageScreen = nullptr;
static BatteryHandler* batteryHandler = nullptr;

// Bench black box (USB-drop investigation). Lives in RTC no-init memory, so it survives every
// reset that keeps the chip powered (software, watchdog, brownout, the RST button) and is garbage
// after a real power loss. The heartbeat rewrites it every 30 s; boot reads the previous record
// before the first write. sysInfo reports both the previous record and the PMU registers at boot.
struct BenchBlackBox {
    uint32_t magic;
    uint32_t seq;
    uint32_t uptimeS;
    uint8_t usbHost;
    uint8_t mode;
    uint8_t pmuStatus1;
    uint8_t pmuStatus2;
    uint8_t pmuOff;
    uint8_t irq1;
    uint8_t irq2;
    uint8_t irq3;
    uint32_t check;
};
static constexpr uint32_t kBlackBoxMagic = 0x51424258u;  // "QBBX"
// Two slots written alternately: a reset in the middle of a write leaves the other slot intact.
static RTC_NOINIT_ATTR BenchBlackBox s_blackBox[2];
static BenchBlackBox s_previousRun;
static bool s_previousRunValid = false;
static BatteryHandler::PmuSnapshot s_bootPmu;

static uint32_t blackBoxChecksum(const BenchBlackBox& b) {
    uint32_t c = b.magic ^ (b.seq * 2654435761u) ^ (b.uptimeS * 40503u);
    c ^= static_cast<uint32_t>(b.usbHost) << 24;
    c ^= static_cast<uint32_t>(b.mode) << 16;
    c ^= static_cast<uint32_t>(b.pmuStatus1) << 8;
    c ^= b.pmuStatus2;
    c ^= static_cast<uint32_t>(b.pmuOff) << 12;
    c ^= static_cast<uint32_t>(b.irq1) << 20;
    c ^= static_cast<uint32_t>(b.irq2) << 4;
    c ^= static_cast<uint32_t>(b.irq3) << 28;
    return c;
}

static bool blackBoxValid(const BenchBlackBox& b) {
    return b.magic == kBlackBoxMagic && b.check == blackBoxChecksum(b);
}

static void blackBoxBootCheck() {
    const bool valid0 = blackBoxValid(s_blackBox[0]);
    const bool valid1 = blackBoxValid(s_blackBox[1]);
    if (valid0 || valid1) {
        const bool pick1 = valid1 && (!valid0 || s_blackBox[1].seq > s_blackBox[0].seq);
        s_previousRun = pick1 ? s_blackBox[1] : s_blackBox[0];
        s_previousRunValid = true;
        ESP_LOGI(TAG, "Black box: previous run uptime=%us seq=%u usb_host=%u mode=%u pmu=%02x/%02x off=%02x irq=%02x/%02x/%02x (the chip kept power through the last reset)",
                 static_cast<unsigned>(s_previousRun.uptimeS), static_cast<unsigned>(s_previousRun.seq),
                 s_previousRun.usbHost, s_previousRun.mode, s_previousRun.pmuStatus1, s_previousRun.pmuStatus2,
                 s_previousRun.pmuOff, s_previousRun.irq1, s_previousRun.irq2, s_previousRun.irq3);
    } else {
        ESP_LOGI(TAG, "Black box: no record (first boot, or power was lost)");
    }
    for (BenchBlackBox& b : s_blackBox) {
        b = BenchBlackBox{};
        b.magic = kBlackBoxMagic;
        b.check = blackBoxChecksum(b);
    }
}

static uint32_t s_blackBoxSeq = 0;

static void blackBoxWrite(uint32_t uptimeS, bool usbHost, uint8_t mode, const BatteryHandler::PmuSnapshot& pmu) {
    const uint32_t seq = ++s_blackBoxSeq;
    BenchBlackBox& b = s_blackBox[seq & 1u];
    b.magic = 0;  // invalid while being written
    b.seq = seq;
    b.uptimeS = uptimeS;
    b.usbHost = usbHost ? 1 : 0;
    b.mode = mode;
    b.pmuStatus1 = pmu.status1;
    b.pmuStatus2 = pmu.status2;
    b.pmuOff = pmu.pwroffStatus;
    b.irq1 = pmu.irq1;
    b.irq2 = pmu.irq2;
    b.irq3 = pmu.irq3;
    b.magic = kBlackBoxMagic;
    b.check = blackBoxChecksum(b);
}
static SDCard* sdCard = nullptr;
static Speaker* speaker = nullptr;
static Microphone* microphone = nullptr;
static SoundPlayer* soundPlayer = nullptr;
static SoundRecorder* soundRecorder = nullptr;
static LocalAudio* localAudio = nullptr;
static input::InputRouter* inputRouter = nullptr;      // INPT-07: sole owner of the touch panel and the knob link
static input::InputSfxListener* inputSfx = nullptr;    // INPT-13
static OTAManager* otaManager = nullptr;
static SleepHandler* sleepHandler = nullptr;

// Pollers (network task) that push driver state into the model
static adapters::ConnectivityPoller* connectivityPoller = nullptr;
static adapters::EnvironmentPoller* environmentPoller = nullptr;
static adapters::BatteryPoller* batteryPoller = nullptr;
static adapters::MqttModelBridge* ledBridge = nullptr;       // LED-03: the LED family over MQTT


// Task handles
static TaskHandle_t displayTaskHandle = nullptr;
static TaskHandle_t networkTaskHandle = nullptr;
static TaskHandle_t commandTaskHandle = nullptr;
static constexpr uint32_t STARTUP_SOUND_TASK_STACK_SIZE = 8192;

// Main task for display and UI: the single consumer of the mutation queue and the only writer
// of the models (D1).
void displayTask(void* parameter) {
    adapters::model_host::adoptCurrentTaskAsConsumer();
    Mode lastModeSeen = INITIALIZATION;
    uint32_t lastRouterActivityMs = 0;
    uint32_t lastRouterOverflow = 0;
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
        const uint32_t frameNow = nowMs();
        // SCRN-09: before app_main publishes the composition, only the boot controller runs.
        const bool ready = s_compositionComplete.load(std::memory_order_acquire);
        if (ready && ledBridge != nullptr) {
            ledBridge->pumpInbound(nowMs());  // LED-03: inbound frames into the registry, before the drain (64-bit clock: the LED windows must not wrap)
        }
        appModel.drainPendingMutations(frameNow);
        if (ready && inputRouter != nullptr) {
            inputRouter->poll(frameNow);
            if (inputRouter->lastActivityMs() != lastRouterActivityMs) {
                lastRouterActivityMs = inputRouter->lastActivityMs();
                displayController->noteActivity();
                if (sleepHandler) {
                    sleepHandler->registerActivity();   // PERI-09: activity reaches the sleep handler from here
                }
            }
            input::InputEvent ev;
            while (inputRouter->next(ev)) {
                ESP_LOGI(TAG, "input %s %s x=%d y=%d zone=%s d=%u k=%d", input::toString(ev.type),
                         input::toString(ev.gesture), ev.x, ev.y, input::toString(ev.zone),
                         static_cast<unsigned>(ev.durationMs), ev.knobDelta);
                if (inputSfx != nullptr) {
                    inputSfx->onEvent(ev);
                }
                switch (ev.type) {   // INPT-05 moved here: the model sees every knob token once
                    case input::InputType::KnobDelta:
                        appModel.mutations().post(model::KnobModel::make(model::KnobModel::Field::Delta, ev.knobDelta, frameNow));
                        break;
                    case input::InputType::KnobBurst:
                        appModel.mutations().post(model::KnobModel::make(model::KnobModel::Field::Burst, ev.knobDelta, frameNow));
                        break;
                    case input::InputType::KnobLink:
                        appModel.mutations().post(model::KnobModel::make(model::KnobModel::Field::LinkActivity, 0, frameNow));
                        break;
                    default:
                        break;
                }
                displayController->onInput(ev);
            }
            if (inputRouter->overflow() != lastRouterOverflow) {
                lastRouterOverflow = inputRouter->overflow();
                ESP_LOGW(TAG, "input ring overflow=%lu high-water=%u", static_cast<unsigned long>(lastRouterOverflow),
                         static_cast<unsigned>(inputRouter->highWater()));
            }
        }

        displayController->update();
        if (ready && ledBridge != nullptr) {
            ledBridge->pumpOutbound(nowMs());  // LED-03: pairing intents, then the publishes
        }

        if (ready && inputRouter != nullptr) {
            // INPT-11 / INPT-16: a mode change arms the wake consumption (SLEEP) or the 250 ms entry
            // window (any other screen), so the press that caused the change never acts on the
            // screen it entered.
            const Mode modeAfter = displayController->getMode();
            if (modeAfter != lastModeSeen) {
                if (modeAfter == SLEEP) {
                    inputRouter->armWakeConsumption();
                } else {
                    inputRouter->beginStaleWindow(frameNow);
                }
                lastModeSeen = modeAfter;
            }
        }

        // Check power management
        if (ready && sleepHandler) {
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

        // Bench heartbeat: one line every 30 s so a long capture shows what preceded a USB drop.
        static int64_t lastHeartbeatMs = 0;
        if (now - lastHeartbeatMs >= 30000) {
            lastHeartbeatMs = now;
            bool usbHost = false;
#if CONFIG_USJ_ENABLE_USB_SERIAL_JTAG
            usbHost = usb_serial_jtag_is_connected();
#endif
            const BatteryHandler::PmuSnapshot pmu =
                batteryHandler ? batteryHandler->readPmuSnapshot() : BatteryHandler::PmuSnapshot{};
            const uint8_t modeCode = displayController ? static_cast<uint8_t>(displayController->getMode()) : 255;
            ESP_LOGI(TAG, "heartbeat uptime=%llds usb_host=%s heap=%u min_heap=%u mode=%s pmu=%02x/%02x off=%02x irq=%02x/%02x/%02x",
                     static_cast<long long>(now / 1000), usbHost ? "attached" : "absent",
                     static_cast<unsigned>(esp_get_free_heap_size()),
                     static_cast<unsigned>(esp_get_minimum_free_heap_size()),
                     displayController ? displayController->getModeName().c_str() : "-",
                     pmu.status1, pmu.status2, pmu.pwroffStatus, pmu.irq1, pmu.irq2, pmu.irq3);
            blackBoxWrite(static_cast<uint32_t>(now / 1000), usbHost, modeCode, pmu);
        }

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
    ESP_LOGI(TAG, "Reset reason code %d (see sysInfo for the name)", static_cast<int>(esp_reset_reason()));
    blackBoxBootCheck();
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
    {
        char pmuLine[224];
        s_bootPmu = batteryHandler->readPmuSnapshot();
        BatteryHandler::formatPmuSnapshot(s_bootPmu, pmuLine, sizeof(pmuLine));
        ESP_LOGI(TAG, "PMU at boot: %s", pmuLine);
    }

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

    // SCRN-08 / SCRN-09: the boot screen is a view driven by its controller from the display task,
    // which starts now so the arc animates through the peripheral and network setup below. The
    // models are sealed first (MDL-30) so the first drain never races a subscription.
    appModel.wireSubscriptions();
    initializationView = new ui::InitializationView();
    bootController = new controllers::BootScreenController(initializationView, &modelQueue, &appModel.navigation(),
                                                           displayPanelBackend->width(), displayPanelBackend->height());
    displayController = new DisplayController();
    displayController->registerModelQueue(&modelQueue);                // INPT-05 + navigation shadow
    displayController->registerBootController(bootController);
    displayController->init();                                         // one ramp, one start stamp (DISP-09)
#if DUAL_CORE_AVAILABLE
    xTaskCreatePinnedToCore(displayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle, DISPLAY_CORE);
#else
    xTaskCreate(displayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle);
#endif
    ESP_LOGI(TAG, "Boot screen up, display task started");

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
    // LED-03: the LED strips over the same broker; frames cross from the MQTT task to the display task.
    ledBridge = new adapters::MqttModelBridge(appModel, *mqttManager);
    ledBridge->begin(nvsManager != nullptr ? nvsManager->deviceName.c_str() : "QNOB");
    environmentPoller = new adapters::EnvironmentPoller(internetHandler->getTimeHandler(),
                                                        internetHandler->getWeatherHandler(), modelQueue);

    // Initialize UI controllers
    ESP_LOGI(TAG, "Initializing UI controllers...");
    modeController = new ModeController();

    // Create EnvironmentInfoScreen (time, weather, temperature). NET-11: the callbacks read the
    // model, which the network-task pollers fill; the screen still owns its own fields.
    infoScreen = new EnvironmentInfoScreen();
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
    infoScreen->setIndoorTempCallback([]() {
        const model::EnvironmentState& e = appModel.environment().state();
        return e.indoorValid ? e.indoorTemp : 18.0f;   // the old placeholder until the first sample
    });

    // Create DeviceInfoScreen (WiFi, internet, MQTT, battery)
    deviceInfoScreen = new DeviceInfoScreen();
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
    placeholderPageScreen = new PlaceholderPageScreen();
    placeholderPageScreen->setPlayPauseAction([]() {
        if (soundController != nullptr) {
            soundController->togglePlayPause();
        }
    });

    soundController = new SoundController(displayPanelBackend.get());
    soundController->setModelQueue(&modelQueue);                       // CTRL-07
    lightView = new ui::LightView();                                   // LED-05: the LIGHT screen on the registry
    lightScreen = new controllers::LightScreenController(appModel, lightView, displayPanelBackend->width(),
                                                         displayPanelBackend->height());
    displayController->registerModeController(modeController);
    displayController->registerInfoScreen(infoScreen);
    displayController->registerDeviceInfoScreen(deviceInfoScreen);
    displayController->registerPlaceholderScreen(placeholderPageScreen);
    displayController->registerSoundController(soundController);
    displayController->registerLightScreen(lightScreen);
    // INPT-12: the router polls both drivers once per frame; the legacy screens still read the
    // latched touch flags in the same frame until WP2.3 cuts them over.
    inputRouter = new input::InputRouter(touchPanel, knobController);
    inputRouter->configure({displayPanelBackend->width(), displayPanelBackend->height(), false});
    inputSfx = new input::InputSfxListener(localAudio);
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
    commandHandler->setModelQueue(&modelQueue);                         // INPT-22
    commandHandler->registerLocalAudio(localAudio);
    commandHandler->registerOTAManager(otaManager);
    commandHandler->registerLedBridge(ledBridge);
    commandHandler->registerDiagnosticsProvider([]() {
        char line[224];
        if (s_previousRunValid) {
            printf("sysInfo: previous run uptime=%us seq=%u usb_host=%u mode=%u pmu=%02x/%02x off=%02x irq=%02x/%02x/%02x (chip kept power)\n",
                   static_cast<unsigned>(s_previousRun.uptimeS), static_cast<unsigned>(s_previousRun.seq),
                   s_previousRun.usbHost, s_previousRun.mode, s_previousRun.pmuStatus1, s_previousRun.pmuStatus2,
                   s_previousRun.pmuOff, s_previousRun.irq1, s_previousRun.irq2, s_previousRun.irq3);
        } else {
            printf("sysInfo: previous run: no record (first boot, or power was lost)\n");
        }
        BatteryHandler::formatPmuSnapshot(s_bootPmu, line, sizeof(line));
        printf("sysInfo: boot %s\n", line);
        if (batteryHandler != nullptr) {
            BatteryHandler::formatPmuSnapshot(batteryHandler->readPmuSnapshot(), line, sizeof(line));
            printf("sysInfo: now %s\n", line);
        }
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

    // Set global MQTT message callback (runs on the MQTT task). MDL-31: every value that reaches
    // a screen from here also reaches the model as a record; nothing writes a model directly.
    mqttManager->setMessageCallback([&](const std::string& topic, const std::string& payload) {
        if (ledBridge != nullptr && ledBridge->onMqttMessage(topic, payload)) {
            return;  // LED-03: queued for the display task
        }
        if (topic == MQTT_TOPIC_COMMAND) {
            if (commandHandler) {
                commandHandler->handleExternalCommand(payload);
            }
        } else if (topic == MQTT_TOPIC_INDOOR_TEMP) {
            // Indoor temperature from bridge (e.g., Home Assistant → MQTT)
            try {
                float indoorTemp = std::stof(payload);
                const uint32_t atMs = static_cast<uint32_t>(nowMs());
                appModel.mutations().post(model::EnvironmentModel::makeFloat(model::EnvironmentModel::Field::IndoorTemp, indoorTemp, atMs));
                appModel.mutations().post(model::ClimateModel::make(model::ClimateModel::Field::IndoorSampled, 0, atMs));
            } catch (...) {
                ESP_LOGW(TAG, "Invalid indoor temperature payload: %s", payload.c_str());
            }
        } else if (topic.find("sound") != std::string::npos) {
            soundController->onMQTTMessage(payload);
        }
    });

    // SCRN-09: the display task has run since the boot screen went up; from here on it may use
    // the input router, the sleep handler and the screens, and honour the boot handoff.
    s_compositionComplete.store(true, std::memory_order_release);
    displayController->markCompositionComplete();

    ESP_LOGI(TAG, "Initialization complete. Starting tasks...");

    // Start tasks
#if DUAL_CORE_AVAILABLE
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle, NETWORK_CORE);
    xTaskCreatePinnedToCore(commandTask, "CommandTask", COMMAND_STACK_SIZE, nullptr, COMMAND_TASK_PRIORITY, &commandTaskHandle, NETWORK_CORE);
#else
    xTaskCreate(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle);
    xTaskCreate(commandTask, "CommandTask", COMMAND_STACK_SIZE, nullptr, COMMAND_TASK_PRIORITY, &commandTaskHandle);
#endif

    if (startupChimeReady) {
        xTaskCreate(startupSoundTask, "StartupSound", STARTUP_SOUND_TASK_STACK_SIZE, soundPlayer, 2, nullptr);
    }

    ESP_LOGI(TAG, "Tasks started.");
}
