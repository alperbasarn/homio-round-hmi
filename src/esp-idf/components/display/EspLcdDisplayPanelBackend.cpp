#include "EspLcdDisplayPanelBackend.h"

bool EspLcdDisplayPanelBackend::initialize() {
    if (!displayBackend.initialize()) {
        return false;
    }

    displayBackend.setBrightness(255);
    displayBackend.setDisplayOn(true);
    return true;
}

void EspLcdDisplayPanelBackend::turnOn() {
    displayBackend.setDisplayOn(true);
    displayBackend.setBrightness(255);
}

void EspLcdDisplayPanelBackend::turnOff() {
    displayBackend.setBrightness(0);
    displayBackend.setDisplayOn(false);
}

void EspLcdDisplayPanelBackend::setBrightness(uint8_t brightness) {
    displayBackend.setBrightness(brightness);
}

int EspLcdDisplayPanelBackend::width() const {
    return displayBackend.width();
}

int EspLcdDisplayPanelBackend::height() const {
    return displayBackend.height();
}

DisplayFlushBackend* EspLcdDisplayPanelBackend::flushBackend() {
    return &displayBackend;
}
