#pragma once

#include <cstdint>
#include <memory>

class DisplayPanelBackend;

enum class DisplayBackendKind : uint8_t {
    EspLcd,
};

struct DisplayBackendSelection {
    DisplayBackendKind panelBackendKind;
    const char* panelBackendName;
    bool usesNativePanelBackend;
};

namespace DisplayBackendFactory {

const DisplayBackendSelection& getDisplayBackendSelection();
std::unique_ptr<DisplayPanelBackend> createPanelBackend();

}  // namespace DisplayBackendFactory
