#pragma once

#include <cstdint>

enum class DisplayFlushFormat {
    RGB565_NATIVE,
    RGB565_SWAPPED,
};

class DisplayFlushBackend {
public:
    virtual ~DisplayFlushBackend() = default;

    virtual int width() const = 0;
    virtual int height() const = 0;
    virtual uint32_t preferredDrawBufferLines() const { return 50; }
    virtual uint32_t minimumDrawBufferLines() const { return 5; }
    virtual bool requiresEvenAreaAlignment() const { return false; }

    virtual void beginWrite() = 0;
    virtual void writeImage(int32_t x,
                            int32_t y,
                            int32_t width,
                            int32_t height,
                            const void* pixelData,
                            DisplayFlushFormat format) = 0;
    virtual void endWrite() = 0;
};
