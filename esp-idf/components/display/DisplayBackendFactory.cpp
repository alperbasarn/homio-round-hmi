#include "DisplayBackendFactory.h"

#include "DisplayPanelBackend.h"
#include "EspLcdDisplayPanelBackend.h"
#include "EspLcdGc9a01DisplayPanelBackend.h"
#include "esp_log.h"
#include "hal_config.h"

#include <memory>

namespace {

constexpr const char* TAG = "DisplayBackendFactory";

constexpr DisplayBackendSelection kSelection = {
    DisplayBackendKind::EspLcd,
#if QNOB_DISPLAY_IS_QSPI
    "esp_lcd_qspi_amoled",
#else
    "esp_lcd_spi_gc9a01",
#endif
    true,
};

}  // namespace

namespace DisplayBackendFactory {

const DisplayBackendSelection& getDisplayBackendSelection() {
    return kSelection;
}

std::unique_ptr<DisplayPanelBackend> createPanelBackend() {
    ESP_LOGI(TAG, "Using %s panel backend for %s", kSelection.panelBackendName, QNOB_BOARD_NAME);
#if QNOB_DISPLAY_IS_QSPI
    return std::make_unique<EspLcdDisplayPanelBackend>();
#else
    return std::make_unique<EspLcdGc9a01DisplayPanelBackend>();
#endif
}

}  // namespace DisplayBackendFactory
