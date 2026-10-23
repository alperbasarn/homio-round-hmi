#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "esp_err.h"
#include "sdmmc_cmd.h"

class SDCard {
public:
    explicit SDCard(const std::string& mountPoint = "/sdcard");
    ~SDCard();

    esp_err_t initialize();
    void deinitialize();

    bool isMounted() const { return mounted; }
    bool isCardPresent() const;
    const std::string& getMountPoint() const { return mountPoint; }

    // Media folder structure rooted under /sdcard/qnob
    std::string getRootDir() const;
    std::string getPicturesDir() const;
    std::string getModeLogosDir() const;
    std::string getAnimationsDir() const;
    std::string getInitAnimationsDir() const;
    std::string getSoundsDir() const;
    std::string getSoundEffectsDir() const;
    std::string getReactionSoundsDir() const;
    std::string getRecordingsDir() const;

    esp_err_t ensureFolderStructure() const;

    bool pathExists(const std::string& path) const;

private:
    std::string mountPoint;
    sdmmc_card_t* card;
    bool mounted;
    bool spiBusInitializedByUs;

    std::string normalizePath(const std::string& path) const;
    esp_err_t ensureDir(const std::string& path) const;
};
