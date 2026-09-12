#include "EspLcdGc9a01DisplayPanelBackend.h"

bool EspLcdGc9a01DisplayPanelBackend::initialize() {
    if (!displayBackend.initialize()) {
        return false;
    }

    displayBackend.setDisplayOn(true);
    displayBackend.setBrightness(255);
    return true;
}

void EspLcdGc9a01DisplayPanelBackend::turnOn() {
    displayBackend.setDisplayOn(true);
    displayBackend.setBrightness(255);
}

void EspLcdGc9a01DisplayPanelBackend::turnOff() {
    displayBackend.setBrightness(0);
    displayBackend.setDisplayOn(false);
}

void EspLcdGc9a01DisplayPanelBackend::setBrightness(uint8_t brightness) {
    displayBackend.setBrightness(brightness);
}

int EspLcdGc9a01DisplayPanelBackend::width() const {
    return displayBackend.width();
}

int EspLcdGc9a01DisplayPanelBackend::height() const {
    return displayBackend.height();
}

DisplayFlushBackend* EspLcdGc9a01DisplayPanelBackend::flushBackend() {
    return &displayBackend;
}
