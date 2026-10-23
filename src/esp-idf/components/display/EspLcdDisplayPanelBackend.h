#pragma once

#include "DisplayPanelBackend.h"
#include "EspLcdSh8601DisplayFlushBackend.h"

class EspLcdDisplayPanelBackend : public DisplayPanelBackend {
public:
    bool initialize() override;
    void turnOn() override;
    void turnOff() override;
    void setBrightness(uint8_t brightness) override;
    int width() const override;
    int height() const override;
    DisplayFlushBackend* flushBackend() override;

private:
    EspLcdSh8601DisplayFlushBackend displayBackend;
};
