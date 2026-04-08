#include "SoundPlayer.h"

#include <algorithm>
#include <cmath>
#include <array>
#include <cerrno>
#include <cctype>
#include <climits>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <memory>
#include <vector>

#include "EmbeddedSounds.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "third_party/minimp3/minimp3_ex.h"

namespace {
constexpr const char* TAG = "SoundPlayer";
constexpr size_t STREAM_CHUNK_SIZE = 2048;
constexpr size_t MP3_STREAM_SAMPLES = 4096;
constexpr float TWO_PI = 6.28318530717958647692f;
constexpr float PI = 3.14159265358979323846f;
constexpr float DEFAULT_SOFTWARE_VOLUME = 0.25f;
constexpr uint32_t SWIPE_SAMPLE_RATE = 16000;
constexpr float SWIPE_DURATION_MS = 70.0f;
constexpr float SWIPE_ATTACK_MS = 8.0f;
constexpr float SWIPE_DECAY_MS = 24.0f;
constexpr float SWIPE_AMPLITUDE = 0.4f;
constexpr float SWIPE_WOBBLE_HZ = 12.0f;
constexpr float SWIPE_WOBBLE_DEPTH = 0.08f;

std::vector<int16_t> generateTactilePulse(uint32_t sampleRate,
                                          float frequencyHz,
                                          float durationMs,
                                          float attackMs,
                                          float decayMs,
                                          float amplitude) {
    if (sampleRate == 0 || durationMs <= 0.0f) {
        return {};
    }

    const float durationSeconds = durationMs / 1000.0f;
    const size_t totalSamples = static_cast<size_t>(durationSeconds * sampleRate);
    if (totalSamples == 0) {
        return {};
    }

    const size_t attackSamples = static_cast<size_t>((attackMs / 1000.0f) * sampleRate);
    const size_t decaySamples = static_cast<size_t>((decayMs / 1000.0f) * sampleRate);
    const size_t safeAttack = std::min(attackSamples, totalSamples);
    const size_t safeDecay = std::min(decaySamples, totalSamples - safeAttack);
    const size_t sustainSamples = totalSamples - safeAttack - safeDecay;

    std::vector<int16_t> samples(totalSamples);
    float phase = 0.0f;
    const float phaseStep = TWO_PI * frequencyHz / static_cast<float>(sampleRate);

    for (size_t i = 0; i < totalSamples; ++i) {
        float env = 1.0f;
        if (safeAttack > 0 && i < safeAttack) {
            const float progress = static_cast<float>(i) / static_cast<float>(safeAttack);
            env = 0.5f - 0.5f * std::cos(PI * progress);
        } else if (safeDecay > 0 && i >= safeAttack + sustainSamples) {
            const size_t remaining = totalSamples - 1 - i;
            const float progress = static_cast<float>(remaining) / static_cast<float>(safeDecay);
            env = 0.5f - 0.5f * std::cos(PI * progress);
        }

        const float base = std::sin(phase);
        float sample = base * amplitude * env;
        sample = std::max(-1.0f, std::min(1.0f, sample));
        samples[i] = static_cast<int16_t>(sample * 32767.0f);

        phase += phaseStep;
        if (phase > TWO_PI) {
            phase -= TWO_PI;
        }
    }

    return samples;
}

float smoothstep(float t) {
    if (t <= 0.0f) {
        return 0.0f;
    }
    if (t >= 1.0f) {
        return 1.0f;
    }
    return t * t * (3.0f - 2.0f * t);
}

std::vector<int16_t> generateSurfSweep(uint32_t sampleRate,
                                       float startHz,
                                       float endHz,
                                       float durationMs,
                                       float attackMs,
                                       float decayMs,
                                       float amplitude,
                                       float wobbleHz,
                                       float wobbleDepth) {
    if (sampleRate == 0 || durationMs <= 0.0f) {
        return {};
    }

    const float durationSeconds = durationMs / 1000.0f;
    const size_t totalSamples = static_cast<size_t>(durationSeconds * sampleRate);
    if (totalSamples == 0) {
        return {};
    }

    const size_t attackSamples = static_cast<size_t>((attackMs / 1000.0f) * sampleRate);
    const size_t decaySamples = static_cast<size_t>((decayMs / 1000.0f) * sampleRate);
    const size_t safeAttack = std::min(attackSamples, totalSamples);
    const size_t safeDecay = std::min(decaySamples, totalSamples - safeAttack);
    const size_t sustainSamples = totalSamples - safeAttack - safeDecay;

    const float invSampleRate = 1.0f / static_cast<float>(sampleRate);
    const float lfoStep = (wobbleHz > 0.0f) ? (TWO_PI * wobbleHz * invSampleRate) : 0.0f;

    std::vector<int16_t> samples(totalSamples);
    float phase = 0.0f;
    float lfoPhase = 0.0f;

    for (size_t i = 0; i < totalSamples; ++i) {
        float env = 1.0f;
        if (safeAttack > 0 && i < safeAttack) {
            const float progress = static_cast<float>(i) / static_cast<float>(safeAttack);
            env = 0.5f - 0.5f * std::cos(PI * progress);
        } else if (safeDecay > 0 && i >= safeAttack + sustainSamples) {
            const size_t remaining = totalSamples - 1 - i;
            const float progress = static_cast<float>(remaining) / static_cast<float>(safeDecay);
            env = 0.5f - 0.5f * std::cos(PI * progress);
        }

        const float progress = (totalSamples > 1)
            ? static_cast<float>(i) / static_cast<float>(totalSamples - 1)
            : 1.0f;
        const float sweep = smoothstep(progress);
        const float freq = startHz + (endHz - startHz) * sweep;
        const float phaseStep = TWO_PI * freq * invSampleRate;

        phase += phaseStep;
        if (phase > TWO_PI) {
            phase -= TWO_PI;
        }

        float wobble = 1.0f;
        if (wobbleDepth > 0.0f && wobbleHz > 0.0f) {
            wobble = 1.0f + wobbleDepth * std::sin(lfoPhase);
            lfoPhase += lfoStep;
            if (lfoPhase > TWO_PI) {
                lfoPhase -= TWO_PI;
            }
        }

        float sample = std::sin(phase) * amplitude * env * wobble;
        sample = std::max(-1.0f, std::min(1.0f, sample));
        samples[i] = static_cast<int16_t>(sample * 32767.0f);
    }

    return samples;
}

struct Mp3FileContext {
    FILE* file = nullptr;
};

size_t mp3ReadCallback(void* buf, size_t size, void* userData) {
    auto* ctx = static_cast<Mp3FileContext*>(userData);
    if (ctx == nullptr || ctx->file == nullptr || buf == nullptr || size == 0) {
        return 0;
    }
    return fread(buf, 1, size, ctx->file);
}

int mp3SeekCallback(uint64_t position, void* userData) {
    auto* ctx = static_cast<Mp3FileContext*>(userData);
    if (ctx == nullptr || ctx->file == nullptr) {
        return -1;
    }
    if (position > static_cast<uint64_t>(LONG_MAX)) {
        return -1;
    }
    return fseek(ctx->file, static_cast<long>(position), SEEK_SET);
}

inline uint16_t readLe16(const uint8_t* p) {
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

inline uint32_t readLe32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (p[3] << 24));
}

std::string lowercase(const std::string& value) {
    std::string out = value;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string basenameNoExt(const std::string& fileName) {
    const size_t slash = fileName.find_last_of('/');
    const std::string justName = (slash == std::string::npos) ? fileName : fileName.substr(slash + 1);
    const size_t dot = justName.find_last_of('.');
    if (dot == std::string::npos || dot == 0) {
        return justName;
    }
    return justName.substr(0, dot);
}

bool hasAudioExtension(const std::string& nameLower) {
    return (nameLower.size() >= 4 && nameLower.rfind(".wav") == nameLower.size() - 4) ||
           (nameLower.size() >= 4 && nameLower.rfind(".mp3") == nameLower.size() - 4);
}

std::string listDirectoryEntries(const std::string& path, size_t maxItems = 64) {
    DIR* dir = opendir(path.c_str());
    if (dir == nullptr) {
        return std::string("<opendir failed errno=") + std::to_string(errno) + ">";
    }

    std::string out;
    size_t count = 0;
    struct dirent* entry = nullptr;
    while ((entry = readdir(dir)) != nullptr) {
        const std::string name = entry->d_name;
        if (name == "." || name == "..") {
            continue;
        }
        if (!out.empty()) {
            out += ", ";
        }
        out += name;
        count++;
        if (count >= maxItems) {
            out += ", ...";
            break;
        }
    }
    closedir(dir);

    return out.empty() ? "<none>" : out;
}

}  // namespace

SoundPlayer::SoundPlayer(SDCard* sdCardValue, Speaker* speakerValue)
    : sdCard(sdCardValue), speaker(speakerValue), ready(false) {
}

esp_err_t SoundPlayer::initialize() {
    if (speaker == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    // SD card is optional - embedded sounds work without it
    ready = true;
    setVolume(DEFAULT_SOFTWARE_VOLUME);
    return ESP_OK;
}

void SoundPlayer::setVolume(float volume) {
    if (speaker != nullptr) {
        speaker->setSoftwareVolume(volume);
    }
}

esp_err_t SoundPlayer::playEffectSound(const std::string& fileName) {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::string baseName = lowercase(basenameNoExt(fileName));
    const bool isPressEffect = (baseName == "click" || baseName == "tap" ||
                                baseName == "touch_press" || baseName == "press");
    const bool isReleaseEffect = (baseName == "release" || baseName == "up" ||
                                  baseName == "touch_release");
    const bool isSwipeLeftEffect = (baseName == "swipe_left" || baseName == "swipeleft" ||
                                    baseName == "left_swipe");
    const bool isSwipeRightEffect = (baseName == "swipe_right" || baseName == "swiperight" ||
                                     baseName == "right_swipe");
    const bool isSwipeUpEffect = (baseName == "swipe_up" || baseName == "swipeup" ||
                                  baseName == "up_swipe");
    const bool isSwipeDownEffect = (baseName == "swipe_down" || baseName == "swipedown" ||
                                    baseName == "down_swipe");

    // Prefer embedded touch effects even when SD card is present.
    if (isPressEffect) {
        ESP_LOGI(TAG, "Using embedded click sound");
        return playClickSound();
    }
    if (isReleaseEffect) {
        ESP_LOGI(TAG, "Using embedded release sound");
        return playReleaseSound();
    }
    if (isSwipeLeftEffect) {
        ESP_LOGI(TAG, "Using embedded swipe-left sound");
        return playSwipeSound(SwipeDirection::Left);
    }
    if (isSwipeRightEffect) {
        ESP_LOGI(TAG, "Using embedded swipe-right sound");
        return playSwipeSound(SwipeDirection::Right);
    }
    if (isSwipeUpEffect) {
        ESP_LOGI(TAG, "Using embedded swipe-up sound");
        return playSwipeSound(SwipeDirection::Up);
    }
    if (isSwipeDownEffect) {
        ESP_LOGI(TAG, "Using embedded swipe-down sound");
        return playSwipeSound(SwipeDirection::Down);
    }

    // Check if SD card is available
    if (sdCard == nullptr || !sdCard->isMounted()) {
        // Fall back to embedded sounds based on filename
        if (isPressEffect) {
            ESP_LOGI(TAG, "SD card not available, using embedded click sound");
            return playClickSound();
        }
        if (isReleaseEffect) {
            ESP_LOGI(TAG, "SD card not available, using embedded release sound");
            return playReleaseSound();
        }
        ESP_LOGW(TAG, "SD card not available and no embedded sound for: %s", fileName.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    const std::string effectPath = sdCard->getSoundEffectsDir() + "/" + fileName;

    // Debug: Try direct file open to diagnose discovery issues
    FILE* debugFile = fopen(effectPath.c_str(), "rb");
    if (debugFile != nullptr) {
        fseek(debugFile, 0, SEEK_END);
        long size = ftell(debugFile);
        fclose(debugFile);
        ESP_LOGI(TAG, "Direct fopen succeeded: %s (size=%ld)", effectPath.c_str(), size);
    } else {
        ESP_LOGW(TAG, "Direct fopen failed: %s (errno=%d: %s)", effectPath.c_str(), errno, strerror(errno));
    }

    if (sdCard->pathExists(effectPath)) {
        esp_err_t err = playFile(effectPath);
        return err;
    }

    std::string fallbackPath1;
    std::string fallbackPath2;

    // Friendly fallback between WAV and MP3 with same basename.
    const std::string lower = lowercase(fileName);
    if (lower.size() >= 4 && lower.rfind(".wav") == lower.size() - 4) {
        fallbackPath1 = sdCard->getSoundEffectsDir() + "/" +
                        fileName.substr(0, fileName.size() - 4) + ".mp3";
    } else if (lower.size() >= 4 && lower.rfind(".mp3") == lower.size() - 4) {
        fallbackPath1 = sdCard->getSoundEffectsDir() + "/" +
                        fileName.substr(0, fileName.size() - 4) + ".wav";
    } else {
        fallbackPath1 = sdCard->getSoundEffectsDir() + "/" + fileName + ".wav";
        fallbackPath2 = sdCard->getSoundEffectsDir() + "/" + fileName + ".mp3";
    }

    if (!fallbackPath1.empty() && sdCard->pathExists(fallbackPath1)) {
        esp_err_t err = playFile(fallbackPath1);
        return err;
    }
    if (!fallbackPath2.empty() && sdCard->pathExists(fallbackPath2)) {
        esp_err_t err = playFile(fallbackPath2);
        return err;
    }

    // Last-resort lookup: scan effect directory and match basename (case-insensitive).
    const std::string requestedBaseLower = lowercase(basenameNoExt(fileName));
    DIR* dir = opendir(sdCard->getSoundEffectsDir().c_str());
    if (dir != nullptr) {
        std::string firstMatchedPath;
        std::string listed;
        struct dirent* entry = nullptr;
        while ((entry = readdir(dir)) != nullptr) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }

            const std::string nameLower = lowercase(name);
            if (listed.size() < 512) {
                if (!listed.empty()) {
                    listed += ", ";
                }
                listed += name;
            }

            if (!hasAudioExtension(nameLower)) {
                continue;
            }
            const std::string baseLower = lowercase(basenameNoExt(name));
            if (baseLower == requestedBaseLower) {
                firstMatchedPath = sdCard->getSoundEffectsDir() + "/" + name;
                break;
            }
        }
        closedir(dir);

        if (!firstMatchedPath.empty()) {
            ESP_LOGW(TAG, "Effect resolved via directory scan: %s", firstMatchedPath.c_str());
            return playFile(firstMatchedPath);
        }

        ESP_LOGE(TAG, "Effect file not found: %s%s%s%s. Available entries: [%s]",
                 effectPath.c_str(),
                 fallbackPath1.empty() ? "" : " (or ",
                 fallbackPath1.empty() ? "" : fallbackPath1.c_str(),
                 fallbackPath1.empty() ? "" : ")",
                 listed.empty() ? "<none>" : listed.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGE(TAG, "Effect file not found: %s%s%s%s. Available entries: [%s]",
             effectPath.c_str(),
             fallbackPath1.empty() ? "" : " (or ",
             fallbackPath1.empty() ? "" : fallbackPath1.c_str(),
             fallbackPath1.empty() ? "" : ")",
             listDirectoryEntries(sdCard->getSoundEffectsDir()).c_str());
    return ESP_ERR_NOT_FOUND;
}

esp_err_t SoundPlayer::playReactionSound(const std::string& fileName) {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    return playFile(sdCard->getReactionSoundsDir() + "/" + fileName);
}

esp_err_t SoundPlayer::playFile(const std::string& path) {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }

    const std::string extLower = lowercase(path);
    if (extLower.size() >= 4 && extLower.rfind(".wav") == extLower.size() - 4) {
        return playWav(path);
    }
    if (extLower.size() >= 4 && extLower.rfind(".mp3") == extLower.size() - 4) {
        return playMp3(path);
    }
    if (extLower.size() >= 4 && extLower.rfind(".pcm") == extLower.size() - 4) {
        return playPcm(path, 24000, 2, 16);
    }

    // Default fallback for raw PCM.
    return playPcm(path, 24000, 2, 16);
}

esp_err_t SoundPlayer::playWav(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open WAV file: %s", path.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    WavInfo info;
    if (!parseWav(file, info)) {
        fclose(file);
        ESP_LOGE(TAG, "Invalid WAV file: %s", path.c_str());
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (info.audioFormat != 1) {  // PCM only
        fclose(file);
        ESP_LOGE(TAG, "Unsupported WAV format (audioFormat=%u)", static_cast<unsigned>(info.audioFormat));
        return ESP_ERR_NOT_SUPPORTED;
    }

    esp_err_t err = speaker->initialize(info.sampleRate,
                                        static_cast<uint8_t>(info.channels),
                                        static_cast<uint8_t>(info.bitsPerSample));
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    if (fseek(file, static_cast<long>(info.dataOffset), SEEK_SET) != 0) {
        fclose(file);
        return ESP_FAIL;
    }

    std::vector<uint8_t> chunk(STREAM_CHUNK_SIZE);
    uint32_t remaining = info.dataSize;
    while (remaining > 0) {
        const size_t toRead = (remaining > chunk.size()) ? chunk.size() : static_cast<size_t>(remaining);
        const size_t got = fread(chunk.data(), 1, toRead, file);
        if (got == 0) {
            break;
        }

        size_t written = 0;
        err = speaker->write(chunk.data(), got, &written, 2000);
        if (err != ESP_OK || written != got) {
            fclose(file);
            return (err == ESP_OK) ? ESP_FAIL : err;
        }

        remaining -= static_cast<uint32_t>(got);
    }

    fclose(file);
    return ESP_OK;
}

esp_err_t SoundPlayer::playMp3(const std::string& path) {
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open MP3 file: %s", path.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    Mp3FileContext ioCtx = {};
    ioCtx.file = file;
    mp3dec_io_t io = {};
    io.read = mp3ReadCallback;
    io.read_data = &ioCtx;
    io.seek = mp3SeekCallback;
    io.seek_data = &ioCtx;

    std::unique_ptr<mp3dec_ex_t> decoder(new (std::nothrow) mp3dec_ex_t());
    if (!decoder) {
        fclose(file);
        ESP_LOGE(TAG, "Out of memory for MP3 decoder.");
        return ESP_ERR_NO_MEM;
    }
    std::memset(decoder.get(), 0, sizeof(mp3dec_ex_t));

    const int openResult = mp3dec_ex_open_cb(decoder.get(), &io, MP3D_DO_NOT_SCAN);
    if (openResult != 0) {
        fclose(file);
        ESP_LOGE(TAG, "MP3 decoder open failed (%d): %s", openResult, path.c_str());
        return ESP_ERR_INVALID_RESPONSE;
    }

    if (decoder->info.hz <= 0 || decoder->info.channels <= 0) {
        mp3dec_ex_close(decoder.get());
        fclose(file);
        ESP_LOGE(TAG, "Invalid MP3 stream metadata: %s", path.c_str());
        return ESP_ERR_INVALID_RESPONSE;
    }

    esp_err_t err = speaker->initialize(static_cast<uint32_t>(decoder->info.hz),
                                        static_cast<uint8_t>(decoder->info.channels),
                                        16);
    if (err != ESP_OK) {
        mp3dec_ex_close(decoder.get());
        fclose(file);
        return err;
    }

    std::vector<mp3d_sample_t> samples(MP3_STREAM_SAMPLES);
    while (true) {
        const size_t readSamples = mp3dec_ex_read(decoder.get(), samples.data(), samples.size());
        if (readSamples == 0) {
            break;
        }

        const size_t bytes = readSamples * sizeof(mp3d_sample_t);
        size_t written = 0;
        err = speaker->write(reinterpret_cast<const uint8_t*>(samples.data()), bytes, &written, 2000);
        if (err != ESP_OK || written != bytes) {
            if (err == ESP_OK) {
                err = ESP_FAIL;
            }
            break;
        }
    }

    if (err == ESP_OK && decoder->last_error != 0) {
        ESP_LOGE(TAG, "MP3 decode stream error (%d): %s", decoder->last_error, path.c_str());
        err = ESP_ERR_INVALID_RESPONSE;
    }

    mp3dec_ex_close(decoder.get());
    fclose(file);
    return err;
}

esp_err_t SoundPlayer::playPcm(const std::string& path, uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample) {
    FILE* file = fopen(path.c_str(), "rb");
    if (file == nullptr) {
        ESP_LOGE(TAG, "Failed to open PCM file: %s", path.c_str());
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t err = speaker->initialize(sampleRate, channels, bitsPerSample);
    if (err != ESP_OK) {
        fclose(file);
        return err;
    }

    std::vector<uint8_t> chunk(STREAM_CHUNK_SIZE);
    while (true) {
        const size_t got = fread(chunk.data(), 1, chunk.size(), file);
        if (got == 0) {
            break;
        }

        size_t written = 0;
        err = speaker->write(chunk.data(), got, &written, 2000);
        if (err != ESP_OK || written != got) {
            fclose(file);
            return (err == ESP_OK) ? ESP_FAIL : err;
        }
    }

    fclose(file);
    return ESP_OK;
}

bool SoundPlayer::parseWav(FILE* file, WavInfo& info) const {
    std::array<uint8_t, 12> riff = {};
    if (fread(riff.data(), 1, riff.size(), file) != riff.size()) {
        return false;
    }
    if (std::memcmp(riff.data(), "RIFF", 4) != 0 || std::memcmp(riff.data() + 8, "WAVE", 4) != 0) {
        return false;
    }

    bool foundFmt = false;
    bool foundData = false;

    while (!foundFmt || !foundData) {
        std::array<uint8_t, 8> header = {};
        if (fread(header.data(), 1, header.size(), file) != header.size()) {
            return false;
        }

        const uint32_t chunkSize = readLe32(header.data() + 4);
        if (std::memcmp(header.data(), "fmt ", 4) == 0) {
            std::vector<uint8_t> fmt(chunkSize);
            if (fread(fmt.data(), 1, fmt.size(), file) != fmt.size()) {
                return false;
            }
            if (fmt.size() < 16) {
                return false;
            }
            info.audioFormat = readLe16(fmt.data() + 0);
            info.channels = readLe16(fmt.data() + 2);
            info.sampleRate = readLe32(fmt.data() + 4);
            info.bitsPerSample = readLe16(fmt.data() + 14);
            foundFmt = true;
        } else if (std::memcmp(header.data(), "data", 4) == 0) {
            info.dataOffset = static_cast<uint32_t>(ftell(file));
            info.dataSize = chunkSize;
            if (fseek(file, static_cast<long>(chunkSize), SEEK_CUR) != 0) {
                return false;
            }
            foundData = true;
        } else {
            if (fseek(file, static_cast<long>(chunkSize), SEEK_CUR) != 0) {
                return false;
            }
        }

        if (chunkSize & 1U) {
            if (fseek(file, 1, SEEK_CUR) != 0) {
                return false;
            }
        }
    }

    return foundFmt && foundData;
}

esp_err_t SoundPlayer::playClickSound() {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    static std::vector<int16_t> clickSamples = generateTactilePulse(
        EmbeddedSounds::kSampleRate,
        120.0f,
        8.0f,
        1.0f,
        3.0f,
        0.6f);
    if (!clickSamples.empty()) {
        return playEmbeddedPcm(clickSamples.data(),
                               clickSamples.size(),
                               EmbeddedSounds::kSampleRate,
                               EmbeddedSounds::kChannels,
                               EmbeddedSounds::kBitsPerSample);
    }
    return playEmbeddedPcm(EmbeddedSounds::kClickSound,
                           EmbeddedSounds::kClickSoundSamples,
                           EmbeddedSounds::kSampleRate,
                           EmbeddedSounds::kChannels,
                           EmbeddedSounds::kBitsPerSample);
}

esp_err_t SoundPlayer::playReleaseSound() {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    static std::vector<int16_t> releaseSamples = generateTactilePulse(
        EmbeddedSounds::kSampleRate,
        90.0f,
        12.0f,
        2.0f,
        5.0f,
        0.5f);
    if (!releaseSamples.empty()) {
        return playEmbeddedPcm(releaseSamples.data(),
                               releaseSamples.size(),
                               EmbeddedSounds::kSampleRate,
                               EmbeddedSounds::kChannels,
                               EmbeddedSounds::kBitsPerSample);
    }
    return playEmbeddedPcm(EmbeddedSounds::kReleaseSound,
                           EmbeddedSounds::kReleaseSoundSamples,
                           EmbeddedSounds::kSampleRate,
                           EmbeddedSounds::kChannels,
                           EmbeddedSounds::kBitsPerSample);
}

esp_err_t SoundPlayer::playSwipeSound(SwipeDirection direction) {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }

    static const std::vector<int16_t> swipeLeftSamples = generateSurfSweep(
        SWIPE_SAMPLE_RATE, 140.0f, 70.0f, SWIPE_DURATION_MS,
        SWIPE_ATTACK_MS, SWIPE_DECAY_MS, SWIPE_AMPLITUDE,
        SWIPE_WOBBLE_HZ, SWIPE_WOBBLE_DEPTH);
    static const std::vector<int16_t> swipeRightSamples = generateSurfSweep(
        SWIPE_SAMPLE_RATE, 70.0f, 140.0f, SWIPE_DURATION_MS,
        SWIPE_ATTACK_MS, SWIPE_DECAY_MS, SWIPE_AMPLITUDE,
        SWIPE_WOBBLE_HZ, SWIPE_WOBBLE_DEPTH);
    static const std::vector<int16_t> swipeUpSamples = generateSurfSweep(
        SWIPE_SAMPLE_RATE, 110.0f, 190.0f, SWIPE_DURATION_MS,
        SWIPE_ATTACK_MS, SWIPE_DECAY_MS, SWIPE_AMPLITUDE,
        SWIPE_WOBBLE_HZ, SWIPE_WOBBLE_DEPTH);
    static const std::vector<int16_t> swipeDownSamples = generateSurfSweep(
        SWIPE_SAMPLE_RATE, 190.0f, 110.0f, SWIPE_DURATION_MS,
        SWIPE_ATTACK_MS, SWIPE_DECAY_MS, SWIPE_AMPLITUDE,
        SWIPE_WOBBLE_HZ, SWIPE_WOBBLE_DEPTH);

    const std::vector<int16_t>* samples = nullptr;
    switch (direction) {
        case SwipeDirection::Left:
            samples = &swipeLeftSamples;
            break;
        case SwipeDirection::Right:
            samples = &swipeRightSamples;
            break;
        case SwipeDirection::Up:
            samples = &swipeUpSamples;
            break;
        case SwipeDirection::Down:
            samples = &swipeDownSamples;
            break;
        default:
            break;
    }

    if (samples == nullptr || samples->empty()) {
        return ESP_ERR_INVALID_ARG;
    }

    return playEmbeddedPcm(samples->data(),
                           samples->size(),
                           SWIPE_SAMPLE_RATE,
                           EmbeddedSounds::kChannels,
                           EmbeddedSounds::kBitsPerSample);
}

esp_err_t SoundPlayer::playStartupSound() {
    if (!ready) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Playing startup chime");
    constexpr uint32_t sampleRate = EmbeddedSounds::kSampleRate;
    constexpr uint8_t bitsPerSample = EmbeddedSounds::kBitsPerSample;
    constexpr uint8_t outputChannels = 2;
    constexpr float durationSeconds = 2.0f;
    constexpr float rampSeconds = 0.5f;
    constexpr float maxAmplitude = 0.25f;  // Half of the previous startup level.
    constexpr size_t tableSize = 256;
    constexpr size_t framesPerChunk = 256;

    esp_err_t err = speaker->initialize(sampleRate, outputChannels, bitsPerSample);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Speaker init failed: %s", esp_err_to_name(err));
        return err;
    }

    std::array<float, tableSize> sineTable = {};
    for (size_t i = 0; i < tableSize; ++i) {
        const float phase = static_cast<float>(i) / static_cast<float>(tableSize);
        sineTable[i] = std::sin(TWO_PI * phase);
    }

    const float freqs[] = {262.0f, 330.0f, 392.0f, 523.0f};
    constexpr size_t freqCount = sizeof(freqs) / sizeof(freqs[0]);
    std::array<float, freqCount> phases = {};
    std::array<float, freqCount> phaseStep = {};
    for (size_t i = 0; i < freqCount; ++i) {
        phaseStep[i] = (freqs[i] * static_cast<float>(tableSize)) / static_cast<float>(sampleRate);
    }

    const size_t totalFrames = static_cast<size_t>(sampleRate * durationSeconds);
    const size_t rampFrames = static_cast<size_t>(sampleRate * rampSeconds);
    const float invFreqCount = 1.0f / static_cast<float>(freqCount);

    std::array<int16_t, framesPerChunk * outputChannels> buffer = {};
    size_t frameIndex = 0;

    while (frameIndex < totalFrames) {
        const size_t framesThis = std::min(framesPerChunk, totalFrames - frameIndex);

        for (size_t i = 0; i < framesThis; ++i) {
            const size_t sampleIndex = frameIndex + i;
            float env = 1.0f;
            if (rampFrames > 0) {
                if (sampleIndex < rampFrames) {
                    env = static_cast<float>(sampleIndex) / static_cast<float>(rampFrames);
                } else if (sampleIndex >= (totalFrames - rampFrames)) {
                    env = static_cast<float>(totalFrames - sampleIndex) / static_cast<float>(rampFrames);
                }
            }

            float sum = 0.0f;
            for (size_t f = 0; f < freqCount; ++f) {
                const size_t idx = static_cast<size_t>(phases[f]) % tableSize;
                sum += sineTable[idx];
                phases[f] += phaseStep[f];
                if (phases[f] >= static_cast<float>(tableSize)) {
                    phases[f] -= static_cast<float>(tableSize);
                }
            }

            float sample = sum * invFreqCount * maxAmplitude * env;
            sample = std::max(-1.0f, std::min(1.0f, sample));
            const int16_t sampleInt = static_cast<int16_t>(sample * 32767.0f);
            buffer[i * 2] = sampleInt;
            buffer[i * 2 + 1] = sampleInt;
        }

        const size_t byteCount = framesThis * outputChannels * sizeof(int16_t);
        size_t written = 0;

        err = speaker->write(reinterpret_cast<const uint8_t*>(buffer.data()), byteCount, &written, 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Speaker write failed: %s", esp_err_to_name(err));
            return err;
        }
        if (written != byteCount) {
            ESP_LOGW(TAG, "Incomplete write: %zu of %zu", written, byteCount);
            return ESP_FAIL;
        }

        frameIndex += framesThis;
        taskYIELD();
    }

    return ESP_OK;
}

esp_err_t SoundPlayer::playEmbeddedPcm(const int16_t* samples, size_t sampleCount,
                                        uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample) {
    if (samples == nullptr || sampleCount == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Always use stereo for I2S compatibility - most DACs expect stereo data
    constexpr uint8_t outputChannels = 2;

    esp_err_t err = speaker->initialize(sampleRate, outputChannels, bitsPerSample);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Speaker init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Convert mono to stereo by duplicating each sample
    std::vector<int16_t> stereoData(sampleCount * 2);
    for (size_t i = 0; i < sampleCount; ++i) {
        stereoData[i * 2] = samples[i];      // Left channel
        stereoData[i * 2 + 1] = samples[i];  // Right channel
    }

    const size_t byteCount = stereoData.size() * sizeof(int16_t);
    size_t written = 0;

    ESP_LOGI(TAG, "Writing %zu stereo samples (%zu bytes)", stereoData.size(), byteCount);

    err = speaker->write(reinterpret_cast<const uint8_t*>(stereoData.data()), byteCount, &written, 5000);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Speaker write failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Wrote %zu of %zu bytes", written, byteCount);

    if (written != byteCount) {
        ESP_LOGW(TAG, "Incomplete write: %zu of %zu", written, byteCount);
        return ESP_FAIL;
    }

    // Wait for I2S DMA to finish transmitting the audio data
    // Calculate playback duration: (mono_samples * 1000ms) / sample_rate
    const uint32_t durationMs = (sampleCount * 1000) / sampleRate;
    const uint32_t bufferMarginMs = 20;  // Add small margin for DMA buffering

    ESP_LOGD(TAG, "Waiting %lums for audio playback to complete", (unsigned long)(durationMs + bufferMarginMs));
    vTaskDelay(pdMS_TO_TICKS(durationMs + bufferMarginMs));

    return ESP_OK;
}
