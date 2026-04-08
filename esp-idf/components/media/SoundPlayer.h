#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

#include "SDCard.h"
#include "Speaker.h"
#include "esp_err.h"

class SoundPlayer {
public:
    SoundPlayer(SDCard* sdCard, Speaker* speaker);

    esp_err_t initialize();
    void setVolume(float volume);

    esp_err_t playEffectSound(const std::string& fileName);
    esp_err_t playReactionSound(const std::string& fileName);
    esp_err_t playFile(const std::string& path);

    // Embedded sound playback (works without SD card)
    esp_err_t playClickSound();
    esp_err_t playReleaseSound();
    esp_err_t playStartupSound();

    bool hasSdCard() const { return sdCard != nullptr && sdCard->isMounted(); }

private:
    enum class SwipeDirection {
        Left,
        Right,
        Up,
        Down
    };

    struct WavInfo {
        uint16_t audioFormat = 0;
        uint16_t channels = 0;
        uint32_t sampleRate = 0;
        uint16_t bitsPerSample = 0;
        uint32_t dataOffset = 0;
        uint32_t dataSize = 0;
    };

    SDCard* sdCard;
    Speaker* speaker;
    bool ready;

    esp_err_t playMp3(const std::string& path);
    esp_err_t playWav(const std::string& path);
    esp_err_t playPcm(const std::string& path, uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample);
    esp_err_t playEmbeddedPcm(const int16_t* samples, size_t sampleCount,
                               uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample);
    esp_err_t playSwipeSound(SwipeDirection direction);
    bool parseWav(FILE* file, WavInfo& info) const;
};
