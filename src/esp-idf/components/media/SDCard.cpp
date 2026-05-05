#include "SDCard.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <algorithm>
#include <string>
#include <sys/stat.h>
#include <sys/types.h>
#include <vector>

#include "driver/sdspi_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "hal_config.h"

namespace {
constexpr const char* TAG = "SDCard";
constexpr size_t DEFAULT_CHUNK_SIZE = 1024;
constexpr size_t MAX_LOG_ENTRIES_PER_DIR = 64;

void logDirectoryTree(const std::string& rootPath, int maxDepth) {
    if (rootPath.empty()) {
        ESP_LOGW(TAG, "logDirectoryTree called with empty root path");
        return;
    }

    ESP_LOGI(TAG, "Directory tree (%s), maxDepth=%d", rootPath.c_str(), maxDepth);

    const auto walk = [&](const auto& self, const std::string& path, int depth) -> void {
        DIR* dir = opendir(path.c_str());
        if (dir == nullptr) {
            ESP_LOGW(TAG, "%s[!] %s (errno=%d: %s)", std::string(depth * 2, ' ').c_str(),
                     path.c_str(), errno, strerror(errno));
            return;
        }

        std::vector<std::string> dirs;
        std::vector<std::string> files;
        size_t entryCount = 0;
        size_t rawEntryCount = 0;
        bool truncated = false;

        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            rawEntryCount++;
            const std::string name = entry->d_name;

            // Debug: Log ALL raw entries including . and ..
            if (depth == 0 || path.find("/effects") != std::string::npos) {
                ESP_LOGI(TAG, "  RAW entry #%u: '%s' (d_type=%d)",
                         (unsigned)rawEntryCount, name.c_str(), entry->d_type);
            }

            if (name == "." || name == "..") {
                continue;
            }
            entryCount++;
            if (entryCount > MAX_LOG_ENTRIES_PER_DIR) {
                truncated = true;
                break;
            }

            const std::string fullPath = path + "/" + name;
            struct stat st = {};
            if (stat(fullPath.c_str(), &st) != 0) {
                files.push_back(name + " [stat errno=" + std::to_string(errno) + "]");
                continue;
            }

            if (S_ISDIR(st.st_mode)) {
                dirs.push_back(name);
            } else {
                files.push_back(name + " (" + std::to_string(static_cast<long long>(st.st_size)) + "B)");
            }
        }
        ESP_LOGI(TAG, "  Total raw entries in %s: %u", path.c_str(), (unsigned)rawEntryCount);
        closedir(dir);

        std::sort(dirs.begin(), dirs.end());
        std::sort(files.begin(), files.end());

        const std::string indent(depth * 2, ' ');
        for (const std::string& dirName : dirs) {
            ESP_LOGI(TAG, "%s[D] %s", indent.c_str(), dirName.c_str());
            if (depth < maxDepth) {
                self(self, path + "/" + dirName, depth + 1);
            }
        }
        for (const std::string& fileName : files) {
            ESP_LOGI(TAG, "%s[F] %s", indent.c_str(), fileName.c_str());
        }
        if (truncated) {
            ESP_LOGW(TAG, "%s... truncated after %u entries", indent.c_str(),
                     static_cast<unsigned>(MAX_LOG_ENTRIES_PER_DIR));
        }
    };

    walk(walk, rootPath, 0);
}
}  // namespace

SDCard::SDCard(const std::string& mountPointValue)
    : mountPoint(mountPointValue),
      card(nullptr),
      mounted(false),
      spiBusInitializedByUs(false) {
}

SDCard::~SDCard() {
    deinitialize();
}

esp_err_t SDCard::initialize() {
    if (mounted) {
        return ESP_OK;
    }

    spi_bus_config_t busCfg = {};
    busCfg.mosi_io_num = SD_MOSI_PIN;
    busCfg.miso_io_num = SD_MISO_PIN;
    busCfg.sclk_io_num = SD_CLK_PIN;
    busCfg.quadwp_io_num = -1;
    busCfg.quadhd_io_num = -1;
    busCfg.max_transfer_sz = 4096;

    esp_err_t err = spi_bus_initialize(SD_SPI_HOST, &busCfg, SDSPI_DEFAULT_DMA);
    if (err == ESP_OK) {
        spiBusInitializedByUs = true;
    } else if (err == ESP_ERR_INVALID_STATE) {
        // Shared SPI bus can already be initialized by display.
        spiBusInitializedByUs = false;
    } else {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(err));
        return err;
    }

    esp_vfs_fat_sdmmc_mount_config_t mountCfg = {};
    mountCfg.format_if_mount_failed = false;
    mountCfg.max_files = 8;
    mountCfg.allocation_unit_size = 512;

    sdspi_device_config_t slotCfg = SDSPI_DEVICE_CONFIG_DEFAULT();
    slotCfg.gpio_cs = static_cast<gpio_num_t>(SD_CS_PIN);
    slotCfg.host_id = SD_SPI_HOST;

    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_SPI_HOST;

    err = esp_vfs_fat_sdspi_mount(mountPoint.c_str(), &host, &slotCfg, &mountCfg, &card);
    if (err != ESP_OK || card == nullptr) {
        ESP_LOGE(TAG, "SD card mount failed: %s", esp_err_to_name(err));
        if (spiBusInitializedByUs) {
            spi_bus_free(SD_SPI_HOST);
            spiBusInitializedByUs = false;
        }
        card = nullptr;
        return (err == ESP_OK) ? ESP_FAIL : err;
    }

    mounted = true;
    sdmmc_card_print_info(stdout, card);

    // Create folder structure and write a test file
    err = ensureFolderStructure();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create media folder structure: %s", esp_err_to_name(err));
        deinitialize();
        return err;
    }

    // Write a test file to verify ESP32 can write and Windows can read
    FILE* testFile = fopen("/sdcard/esp32_test.txt", "w");
    if (testFile) {
        fprintf(testFile, "Written by ESP32 at boot\n");
        fclose(testFile);
        ESP_LOGI(TAG, "Test file written: /sdcard/esp32_test.txt");
    } else {
        ESP_LOGE(TAG, "Failed to write test file: errno=%d", errno);
    }

    ESP_LOGI(TAG, "SD card mounted at %s", mountPoint.c_str());
    // Debug inventory to verify discovered files/directories on target.
    // Disabled to prevent stack overflow during initialization
    // logDirectoryTree(mountPoint, 2);
    // logDirectoryTree(getRootDir(), 4);
    return ESP_OK;
}

void SDCard::deinitialize() {
    if (mounted && card != nullptr) {
        esp_vfs_fat_sdcard_unmount(mountPoint.c_str(), card);
    }

    if (spiBusInitializedByUs) {
        spi_bus_free(SD_SPI_HOST);
    }

    card = nullptr;
    mounted = false;
    spiBusInitializedByUs = false;
}

bool SDCard::isCardPresent() const {
    if (!mounted || card == nullptr) {
        return false;
    }
    return sdmmc_get_status(card) == ESP_OK;
}

std::string SDCard::getRootDir() const {
    return mountPoint + "/qnob";
}

std::string SDCard::getPicturesDir() const {
    return getRootDir() + "/pics";
}

std::string SDCard::getModeLogosDir() const {
    return getPicturesDir() + "/mode_logos";
}

std::string SDCard::getAnimationsDir() const {
    return getRootDir() + "/animations";
}

std::string SDCard::getInitAnimationsDir() const {
    return getAnimationsDir() + "/init";
}

std::string SDCard::getSoundsDir() const {
    return getRootDir() + "/sounds";
}

std::string SDCard::getSoundEffectsDir() const {
    return getSoundsDir() + "/effects";
}

std::string SDCard::getReactionSoundsDir() const {
    return getSoundsDir() + "/reactions";
}

std::string SDCard::getRecordingsDir() const {
    return getRootDir() + "/recordings";
}

esp_err_t SDCard::ensureFolderStructure() const {
    const std::vector<std::string> dirs = {
        getRootDir(),
        getPicturesDir(),
        getModeLogosDir(),
        getAnimationsDir(),
        getInitAnimationsDir(),
        getSoundsDir(),
        getSoundEffectsDir(),
        getReactionSoundsDir(),
        getRecordingsDir(),
    };

    for (const std::string& dir : dirs) {
        esp_err_t err = ensureDir(dir);
        if (err != ESP_OK) {
            return err;
        }
    }

    ESP_LOGI(TAG, "Media folders ready under %s", getRootDir().c_str());
    return ESP_OK;
}

bool SDCard::pathExists(const std::string& path) const {
    struct stat st = {};
    return stat(normalizePath(path).c_str(), &st) == 0;
}

esp_err_t SDCard::readFile(const std::string& path, std::vector<uint8_t>& outData) const {
    if (!mounted || card == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::string fullPath = normalizePath(path);
    FILE* file = fopen(fullPath.c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for read: %s", fullPath.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return ESP_FAIL;
    }
    const long size = ftell(file);
    if (size < 0) {
        fclose(file);
        return ESP_FAIL;
    }
    rewind(file);

    outData.resize(static_cast<size_t>(size));
    const size_t readBytes = fread(outData.data(), 1, outData.size(), file);
    fclose(file);

    if (readBytes != outData.size()) {
        outData.clear();
        return ESP_FAIL;
    }

    return ESP_OK;
}

esp_err_t SDCard::writeFile(const std::string& path, const uint8_t* data, size_t len, bool append) const {
    if (!mounted || card == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr && len > 0) {
        return ESP_ERR_INVALID_ARG;
    }

    const std::string fullPath = normalizePath(path);
    const char* mode = append ? "ab" : "wb";
    FILE* file = fopen(fullPath.c_str(), mode);
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open file for write: %s", fullPath.c_str());
        return ESP_FAIL;
    }

    size_t written = 0;
    while (written < len) {
        const size_t chunk = (len - written > DEFAULT_CHUNK_SIZE) ? DEFAULT_CHUNK_SIZE : (len - written);
        const size_t chunkWritten = fwrite(data + written, 1, chunk, file);
        if (chunkWritten != chunk) {
            fclose(file);
            return ESP_FAIL;
        }
        written += chunkWritten;
    }

    fclose(file);
    return ESP_OK;
}

esp_err_t SDCard::writeTextFile(const std::string& path, const std::string& text, bool append) const {
    return writeFile(path, reinterpret_cast<const uint8_t*>(text.data()), text.size(), append);
}

std::string SDCard::normalizePath(const std::string& path) const {
    if (path.empty()) {
        return mountPoint;
    }
    if (path[0] == '/') {
        return path;
    }
    return mountPoint + "/" + path;
}

esp_err_t SDCard::ensureDir(const std::string& path) const {
    struct stat st = {};
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return ESP_OK;
        }
        return ESP_FAIL;
    }

    if (mkdir(path.c_str(), 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }

    ESP_LOGE(TAG, "mkdir failed for %s: errno=%d", path.c_str(), errno);
    return ESP_FAIL;
}
