/*
 * QNOB Screen Application - ESP-IDF Version
 *
 * Main entry point for the QNOB smart knob display application.
 *
 * WP1.4: the model root lives here (APP-06). Every existing data path also posts mutation
 * records into it (MDL-31), the pollers on the network task feed it (NET-06, NET-07, PERI-06),
 * the display task drains it at the top of each frame, and the four screen callbacks read the
 * model instead of the drivers (NET-11). The screens themselves are unchanged until Phase 3.
 *
 * Integration with main (2026-10): BootGuard runs first and a safe-mode boot takes its own path
 * (runSafeMode); Bluetooth starts before WiFi, ConnectivityManager owns the radio, mDNS advertises
 * the device, the setup portal and the BLE serial line reach CommandHandler, and the OTA release
 * channel is configured from NVS.
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

// Boot guard (safe-mode escalation), radio ownership, Bluetooth and mDNS (main)
#include "BootGuard.h"
#include "SafeModeScreen.h"
#include "ConnectivityManager.h"
#include "BluetoothManager.h"
#include "MdnsAdvertiser.h"
#include "esp_wifi.h"

// Controllers
#include "CommandHandler.h"
#include "MediaScreenController.h"
#include "MediaView.h"
#include "adapters/FrameRing.h"
#include "media_target/MediaTargetRegistry.h"
#include "media_target/PcMediaAdapter.h"
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
#include "DeviceInfoScreenController.h"
#include "DeviceInfoView.h"
#include "PlaceholderPageScreen.h"
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
#include <cstdio>
#include <string>


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
static ui::MediaView* mediaView = nullptr;                                  // MEDIA-16
static controllers::MediaScreenController* mediaScreen = nullptr;         // MEDIA-16
static media::MediaTargetRegistry* mediaTargets = nullptr;                // MEDIA-03
static media::PcMediaAdapter* pcMedia = nullptr;                          // MEDIA-04
// MEDIA-06: esp32/sound frames cross from the MQTT event task to the display task through this ring.
using SoundRing = adapters::FrameRing<8, 32, 256>;
static SoundRing soundRing;
// The display task publishes here (a detent, a tap, the screen entry under the LVGL lock), so the
// message is enqueued for the MQTT task rather than written on the socket inline; the sender=
// envelope stays what MQTTManager::publish sends, so the PC app sees the same bytes as before.
static bool pcMediaPublish(const char* topic, const char* body, void*) {
    if (mqttManager == nullptr || !mqttManager->isConnected()) {
        return false;
    }
    char full[160];
    const char* name = (nvsManager != nullptr && !nvsManager->deviceName.empty()) ? nvsManager->deviceName.c_str() : "QNOB";
    const int n = std::snprintf(full, sizeof(full), "sender=%s,%s", name, body);
    if (n <= 0 || static_cast<size_t>(n) >= sizeof(full)) {
        return false;
    }
    const bool ok = mqttManager->publishRaw(topic, full, static_cast<size_t>(n), 0, false) == ESP_OK;
    ESP_LOGI(TAG, "media publish %s %s%s", topic, body, ok ? "" : " (refused)");
    return ok;
}
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
static ui::DeviceInfoView* deviceInfoView = nullptr;                        // SCRN-10
static controllers::DeviceInfoScreenController* deviceInfoScreen = nullptr;  // SCRN-11
// The display owns the backlight (SCRN-23); the page moves it through this pair until a row gives
// the display its own model.
static int deviceBrightnessGet(void*) {
    return displayController != nullptr ? displayController->getBrightnessPercent() : 100;
}
static void deviceBrightnessSet(int percent, void*) {
    if (displayController != nullptr) {
        displayController->setBrightnessPercent(percent);
    }
}
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
static BluetoothManager* bluetoothManager = nullptr;
static MdnsAdvertiser* mdnsAdvertiser = nullptr;

// Pollers (network task) that push driver state into the model
static adapters::ConnectivityPoller* connectivityPoller = nullptr;
static adapters::EnvironmentPoller* environmentPoller = nullptr;
static adapters::BatteryPoller* batteryPoller = nullptr;
static adapters::MqttModelBridge* ledBridge = nullptr;       // LED-03: the LED family over MQTT


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

// mDNS restarts with the STA MAC whenever the link changes (main's A3/A7).
static void restartMdns() {
    if (mdnsAdvertiser == nullptr) {
        return;
    }
    uint8_t mac[6] = {};
    esp_wifi_get_mac(WIFI_IF_STA, mac);
    mdnsAdvertiser->start(nvsManager, mac);
}

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
        if (ready && pcMedia != nullptr) {
            // MEDIA-06 / MEDIA-16: the PC replies the MQTT task queued, then the link state, before the drain.
            SoundRing::Frame frame;
            while (soundRing.pop(frame)) {
                pcMedia->onResponse(frame.payload, nowMs());
            }
            pcMedia->setLinkUp(mqttManager != nullptr && mqttManager->isConnected(), nowMs());
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
                    case input::InputType::KnobAck:
                        // PCKNOB-16: the ack clears the pending write and is link activity too.
                        appModel.mutations().post(model::KnobModel::make(model::KnobModel::Field::WriteAck, ev.knobDelta, frameNow));
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
        // PCKNOB-15: a screen entry asked the knob to follow its value (KnobModel::onEnter); the
        // display task owns the model, so the request is taken here and written to the UART. With
        // no knob wired the ack never comes and the model's 1 s timeout adopts the value instead.
        if (ready) {
            const int pendingKnobWrite = appModel.knob().takeWriteRequest(nowMs());
            if (pendingKnobWrite >= 0 && knobController != nullptr) {
                knobController->setSetpoint(pendingKnobWrite);
            }
        }
        if (ready && ledBridge != nullptr) {
            ledBridge->pumpOutbound(nowMs());  // LED-03: pairing intents, then the publishes
        }
        if (ready && mediaTargets != nullptr) {
            mediaTargets->tickAll(nowMs());  // MEDIA-07: the setpoint window and the settling call
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
            if (bluetoothManager != nullptr) {
                // main: the BLE battery service follows the cell (the manager notifies on change only).
                const BatteryHandler::BatteryTelemetry telemetry = batteryHandler->getBatteryTelemetry();
                bluetoothManager->updateBatteryLevel(batteryHandler->isBatteryConnected(), telemetry.percentage);
            }
        }
        if (batteryPoller) {
            batteryPoller->poll(now);
        }

        wifiManager->update();
        if (internetHandler) {
            internetHandler->update();   // owns "internet available"; also services time and weather
        }
        if (mqttManager) {
            mqttManager->update();
        }

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

// main's safe-mode boot (BootGuard levels 1-3): no screens, no MQTT, no media, no Bluetooth, no
// command handler. With a display (Rescue, OfflineRecovery) the panel shows the safe-mode face;
// Rescue and HeadlessRecovery keep the AP and the portal up so the device can be reconfigured.
static void safeModeDisplayTask(void* parameter) {
    (void)parameter;
    while (true) {
        LvglDisplay::taskHandler();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void runSafeMode(bool useDisplay) {
    // No display task drains the model in safe mode, so nothing may post into it.
    delete batteryPoller;
    batteryPoller = nullptr;

    if (useDisplay) {
        displayPanelBackend = DisplayBackendFactory::createPanelBackend();
        if (displayPanelBackend && displayPanelBackend->initialize() &&
            LvglDisplay::init(displayPanelBackend->flushBackend())) {
            LvglDisplay::setPanelBackend(displayPanelBackend.get());
            displayPanelBackend->setBrightness(255);
            static SafeModeScreen safeModeScreen(BootGuard::modeName());
            if (LvglDisplay::lock(-1)) {
                safeModeScreen.show();
                LvglDisplay::unlock();
            }
#if DUAL_CORE_AVAILABLE
            xTaskCreatePinnedToCore(safeModeDisplayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle, DISPLAY_CORE);
#else
            xTaskCreate(safeModeDisplayTask, "DisplayTask", DISPLAY_STACK_SIZE, nullptr, DISPLAY_TASK_PRIORITY, &displayTaskHandle);
#endif
        } else {
            ESP_LOGE(TAG, "Safe mode: display init failed; continuing headless");
        }
    }

    // OfflineRecovery (level 3) skips all networking; WiFiManager exists for pointer stability but
    // is not initialised (main: an AP restart in that mode crashes the event loop).
    ESP_ERROR_CHECK(ConnectivityManager::instance().begin());
    wifiManager = new WiFiManager(nvsManager);
    if (BootGuard::getMode() != BootGuard::SafeMode::OfflineRecovery) {
        wifiManager->initialize();
    } else {
        ESP_LOGW(TAG, "Networking skipped in OfflineRecovery mode");
    }
    wifiManager->connectToWiFi();   // STA is off at every safe level; this only logs

#if DUAL_CORE_AVAILABLE
    xTaskCreatePinnedToCore(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle, NETWORK_CORE);
#else
    xTaskCreate(networkTask, "NetworkTask", NETWORK_STACK_SIZE, nullptr, NETWORK_TASK_PRIORITY, &networkTaskHandle);
#endif

    BootGuard::armHealthTimer();
    ESP_LOGW(TAG, "Safe mode %s running: AP and portal only", BootGuard::modeName());
}

extern "C" void app_main(void) {
    // Boot guard first (main): reads NVS, counts the crash if the previous boot never reached the
    // health timer, escalates the safe-mode level and marks this boot in progress.
    BootGuard::begin();

    if (BootGuard::getMode() == BootGuard::SafeMode::Emergency) {
        ESP_LOGE(TAG, "====================================================");
        ESP_LOGE(TAG, "  EMERGENCY MODE - device has crashed repeatedly.");
        ESP_LOGE(TAG, "  Flash new firmware via USB to recover.");
        ESP_LOGE(TAG, "====================================================");
        for (;;) { vTaskDelay(pdMS_TO_TICKS(1000)); }
    }

    const bool useDisplay = (BootGuard::getMode() != BootGuard::SafeMode::HeadlessRecovery);

    // FIRST: Reset display panel before anything else
    // This fixes white screen issue when serial monitor triggers soft reset
    // or when the panel is left in an undefined state
#if LCD_RST_PIN >= 0
    if (useDisplay) {
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
    } // useDisplay
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

    // Generate pairing token on first boot (A6).
    if (nvsManager->pairToken.empty()) {
        if (nvsManager->generateAndSavePairToken() == ESP_OK) {
            ESP_LOGI(TAG, "Generated new pairing token");
        }
    }

    // Recovery mode: if PWR_KEY is held LOW for >= 3 s during boot, force
    // wifi_ap_enabled and portal_enabled to true for this session (does NOT
    // write NVS) so the user can always reach the portal even if they
    // accidentally disabled it via T-18/T-19. Factory reset also sets this.
#if defined(PWR_KEY_PIN) && (PWR_KEY_PIN >= 0)
    {
        gpio_config_t rcv_cfg = {};
        rcv_cfg.intr_type    = GPIO_INTR_DISABLE;
        rcv_cfg.mode         = GPIO_MODE_INPUT;
        rcv_cfg.pin_bit_mask = (1ULL << PWR_KEY_PIN);
        rcv_cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        gpio_config(&rcv_cfg);

        constexpr int kRecoveryHoldMs  = 3000;
        constexpr int kSampleIntervalMs = 50;
        int held_ms = 0;
        while (held_ms < kRecoveryHoldMs) {
            if (gpio_get_level((gpio_num_t)PWR_KEY_PIN) != 0) {
                break;  // released - not a recovery hold
            }
            vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
            held_ms += kSampleIntervalMs;
        }
        if (held_ms >= kRecoveryHoldMs) {
            nvsManager->recoveryModeActive = true;
            ESP_LOGW(TAG, "*** RECOVERY MODE ACTIVE: AP + portal forced on ***");
            // The display task restarts the chip on a pressed PWR_KEY and starts within a frame of
            // here, so wait for the release first or a long hold would reboot into a loop.
            while (gpio_get_level((gpio_num_t)PWR_KEY_PIN) == 0) {
                vTaskDelay(pdMS_TO_TICKS(kSampleIntervalMs));
            }
        }
    }
#endif

    // Safe-mode flag overrides - applied AFTER NVS load so they take precedence
    // without touching persisted NVS.  WiFiManager and ConnectivityManager read
    // these flags at runtime.
    if (BootGuard::isInSafeMode()) {
        const auto mode = BootGuard::getMode();
        nvsManager->bluetoothEnabled = false;
        if (mode == BootGuard::SafeMode::Rescue ||
            mode == BootGuard::SafeMode::HeadlessRecovery) {
            // AP + portal only; disable STA to avoid long association delays.
            nvsManager->wifiStaEnabled = false;
            nvsManager->wifiApEnabled  = true;
            nvsManager->portalEnabled  = true;
        } else {
            // OfflineRecovery: no network at all.
            nvsManager->wifiStaEnabled = false;
            nvsManager->wifiApEnabled  = false;
            nvsManager->portalEnabled  = false;
        }
        ESP_LOGW(TAG, "Safe-mode overrides applied: %s", BootGuard::modeName());
        runSafeMode(useDisplay);
        return;
    }

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

    // Initialize Bluetooth BEFORE WiFi - BT controller must claim its RF
    // coexistence slot before the WiFi stack starts, otherwise the radio
    // arbitration is misconfigured and BLE cannot advertise or scan.
    ESP_LOGI(TAG, "Initializing Bluetooth...");
    bluetoothManager = &BluetoothManager::instance();
    {
        // Apply persisted enabled flag before begin() so startAdvertising()
        // is skipped when the user previously disabled BT (T-17).
        bluetoothManager->setEnabled(nvsManager->bluetoothEnabled);
        const std::string btName = !nvsManager->bluetoothName.empty()
            ? nvsManager->bluetoothName : "Qnob PC Control";
        const esp_err_t btErr = bluetoothManager->begin(btName.c_str());
        if (btErr != ESP_OK) {
            ESP_LOGW(TAG, "Bluetooth init failed: %s", esp_err_to_name(btErr));
        }
    }

    // ConnectivityManager before WiFi so its task is running and ready to
    // receive radio events as soon as the WiFi stack posts them.
    ESP_LOGI(TAG, "Initializing ConnectivityManager...");
    ESP_ERROR_CHECK(ConnectivityManager::instance().begin());

    // Initialize networking
    ESP_LOGI(TAG, "Initializing networking...");
    wifiManager = new WiFiManager(nvsManager);
    wifiManager->initialize();
    mqttManager = new MQTTManager(wifiManager, nvsManager);
    if (mqttManager->initialize() != ESP_OK) {
        ESP_LOGW(TAG, "MQTT not configured yet (init skipped)");
    }
    internetHandler = new InternetHandler(wifiManager, nvsManager);   // weather settings from NVS first
    // NET-09: one owner of "internet available"; MQTT asks it through a predicate.
    mqttManager->setInternetPredicate([]() {
        return internetHandler != nullptr && internetHandler->isInternetAvailable();
    });
    wifiManager->connectToWiFi();   // NET-30: a request; the network task runs the sequence
    otaManager = new OTAManager(wifiManager);
    applyOtaConfigFromNvs();

    // mDNS (_qnob._tcp, A3/A7): up now on the AP, restarted on every STA edge.
    mdnsAdvertiser = new MdnsAdvertiser();
    wifiManager->setConnectedCallback([](const std::string& /*ssid*/, const std::string& /*ip*/) { restartMdns(); });
    wifiManager->setDisconnectedCallback([]() { restartMdns(); });
    restartMdns();

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

    mediaTargets = new media::MediaTargetRegistry();                   // MEDIA-03
    pcMedia = new media::PcMediaAdapter(&appModel.mutations(), &appModel.media(), &pcMediaPublish, nullptr);  // MEDIA-04
    mediaTargets->registerTarget(pcMedia);

    // DEVICE_INFO (SCRN-10 / SCRN-11): the view reads the connectivity and power models through
    // the controller; the network-details, Bluetooth and software-update callbacks of the old screen
    // are gone with it (none of them was ever drawn, and release updates are not wired in this tree).
    deviceInfoView = new ui::DeviceInfoView();
    deviceInfoScreen = new controllers::DeviceInfoScreenController(appModel, *mediaTargets, deviceInfoView,
                                                                   displayPanelBackend->width(),
                                                                   displayPanelBackend->height());
    deviceInfoScreen->setBrightnessAccess(&deviceBrightnessGet, &deviceBrightnessSet, nullptr);

    // Matrix placeholder pages (timer, light, sound, temperature, PC). The PC page play/pause is a
    // BLE HID media key when a host is paired over Bluetooth (main), otherwise it goes to the PC
    // over the existing sound path through the media target.
    placeholderPageScreen = new PlaceholderPageScreen();
    placeholderPageScreen->setPlayPauseAction([]() {
        if (bluetoothManager != nullptr && bluetoothManager->isHidConnected()) {
            bluetoothManager->sendPlayPause();
            return;
        }
        if (mediaTargets != nullptr) {
            mediaTargets->target(appModel.media().state().activeTarget).toggle(nowMs());
        }
    });

    mediaView = new ui::MediaView();                                   // MEDIA-16: the MEDIA screen on the target registry
    mediaScreen = new controllers::MediaScreenController(appModel, *mediaTargets, mediaView, displayPanelBackend->width(),
                                                         displayPanelBackend->height());
    lightView = new ui::LightView();                                   // LED-05: the LIGHT screen on the registry
    lightScreen = new controllers::LightScreenController(appModel, lightView, displayPanelBackend->width(),
                                                         displayPanelBackend->height());
    displayController->registerModeController(modeController);
    displayController->registerInfoScreen(infoScreen);
    displayController->registerDeviceInfoScreen(deviceInfoScreen);
    displayController->registerPlaceholderScreen(placeholderPageScreen);
    displayController->registerMediaScreen(mediaScreen);
    displayController->registerLightScreen(lightScreen);
    // INPT-12: the router polls both drivers once per frame; the legacy screens still read the
    // latched touch flags in the same frame until WP2.3 cuts them over.
    inputRouter = new input::InputRouter(touchPanel, knobController);
    inputRouter->configure({displayPanelBackend->width(), displayPanelBackend->height(), false});
    inputSfx = new input::InputSfxListener(localAudio);

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
    commandHandler->registerBluetoothManager(bluetoothManager);
    commandHandler->setOtaConfigUpdatedCallback([]() { applyOtaConfigFromNvs(); });
    commandHandler->setPairedStateCallback([](bool paired) {
        if (mdnsAdvertiser != nullptr) mdnsAdvertiser->updatePaired(paired);
    });
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

    // The setup portal's forms reach the command handler as the portal source (main).
    wifiManager->setSetupPortalCommandCallback([](const std::string& cmd) {
        if (commandHandler != nullptr) commandHandler->handleExternalCommand(cmd, CommandSource::Portal);
    });

    // Bridge BLE Nordic-UART RX -> CommandHandler -> BLE TX (A4). The callback fires from the BT
    // event task; CommandHandler serialises dispatch, and an envelope-less line is held to the
    // remote allowlist (APP-03).
    if (bluetoothManager != nullptr) {
        bluetoothManager->setSerialLineCallback([](const std::string& line) {
            if (commandHandler == nullptr) return;
            std::string response;
            commandHandler->handleExternalCommand(line, response);
            if (!response.empty() && bluetoothManager != nullptr) {
                bluetoothManager->sendSerialLine(response);
            }
        });
    }

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
        } else if (topic.rfind("esp32/sound/", 0) == 0) {
            // MEDIA-06: queued for the display task; a full ring drops and counts, never blocks.
            soundRing.push(topic.c_str(), topic.size(), payload.c_str(), payload.size());
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

    // Arm the 15-second health timer.  On expiry it clears the boot_in_progress
    // NVS flag and resets crash_count / safe_mode_level to 0.
    BootGuard::armHealthTimer();

    ESP_LOGI(TAG, "Tasks started.");
}
