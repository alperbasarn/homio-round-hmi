#pragma once

#include "DisplayFlushBackend.h"
#include <cstdint>

class DisplayPanelBackend {
public:
    virtual ~DisplayPanelBackend() = default;

    virtual bool initialize() = 0;
    virtual void turnOn() = 0;
    virtual void turnOff() = 0;
    virtual void setBrightness(uint8_t brightness) = 0;
    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual DisplayFlushBackend* flushBackend() = 0;
};
