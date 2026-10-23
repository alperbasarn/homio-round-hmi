#pragma once

#include "DisplayPanelBackend.h"
#include "EspLcdGc9a01DisplayFlushBackend.h"

class EspLcdGc9a01DisplayPanelBackend : public DisplayPanelBackend {
public:
    bool initialize() override;
    void turnOn() override;
    void turnOff() override;
    void setBrightness(uint8_t brightness) override;
    int width() const override;
    int height() const override;
    DisplayFlushBackend* flushBackend() override;

private:
    EspLcdGc9a01DisplayFlushBackend displayBackend;
};
