#include "Speaker.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "hal_config.h"
#include "hal_init.h"

namespace {
constexpr const char* TAG = "Speaker";
constexpr float MIN_VOLUME = 0.0f;
constexpr float MAX_VOLUME = 1.0f;

// ES8311 Audio Codec registers
constexpr uint8_t ES8311_ADDR = 0x18;
constexpr uint8_t ES8311_REG00_RESET = 0x00;
constexpr uint8_t ES8311_REG01_CLK_MANAGER = 0x01;
constexpr uint8_t ES8311_REG02_CLK_MANAGER = 0x02;
constexpr uint8_t ES8311_REG03_CLK_MANAGER = 0x03;
constexpr uint8_t ES8311_REG04_CLK_MANAGER = 0x04;
constexpr uint8_t ES8311_REG05_CLK_MANAGER = 0x05;
constexpr uint8_t ES8311_REG06_CLK_MANAGER = 0x06;
constexpr uint8_t ES8311_REG07_CLK_MANAGER = 0x07;
constexpr uint8_t ES8311_REG08_CLK_MANAGER = 0x08;
constexpr uint8_t ES8311_REG09_SDP_IN = 0x09;
constexpr uint8_t ES8311_REG0A_SDP_OUT = 0x0A;
constexpr uint8_t ES8311_REG0B_SYSTEM = 0x0B;
constexpr uint8_t ES8311_REG0C_SYSTEM = 0x0C;
constexpr uint8_t ES8311_REG0D_SYSTEM = 0x0D;
constexpr uint8_t ES8311_REG0E_SYSTEM = 0x0E;
constexpr uint8_t ES8311_REG0F_SYSTEM = 0x0F;
constexpr uint8_t ES8311_REG10_SYSTEM = 0x10;
constexpr uint8_t ES8311_REG11_SYSTEM = 0x11;
constexpr uint8_t ES8311_REG12_SYSTEM = 0x12;
constexpr uint8_t ES8311_REG13_SYSTEM = 0x13;
constexpr uint8_t ES8311_REG14_SYSTEM = 0x14;
constexpr uint8_t ES8311_REG15_ADC = 0x15;
constexpr uint8_t ES8311_REG16_ADC = 0x16;
constexpr uint8_t ES8311_REG17_ADC = 0x17;
constexpr uint8_t ES8311_REG18_ADC = 0x18;
constexpr uint8_t ES8311_REG19_ADC = 0x19;
constexpr uint8_t ES8311_REG1A_ADC = 0x1A;
constexpr uint8_t ES8311_REG1B_ADC = 0x1B;
constexpr uint8_t ES8311_REG1C_ADC = 0x1C;
constexpr uint8_t ES8311_REG31_DAC = 0x31;
constexpr uint8_t ES8311_REG32_DAC = 0x32;
constexpr uint8_t ES8311_REG37_DAC = 0x37;
constexpr uint8_t ES8311_REG44_DACGPF = 0x44;
constexpr uint8_t ES8311_REG45_GP = 0x45;
constexpr uint8_t ES8311_REG45_CHIPID = 0xFD;

constexpr uint32_t ES8311_MCLK_DIV = 256;

struct Es8311CoeffDiv {
    uint32_t mclk;
    uint32_t rate;
    uint8_t pre_div;
    uint8_t pre_multi;
    uint8_t adc_div;
    uint8_t dac_div;
    uint8_t fs_mode;
    uint8_t lrck_h;
    uint8_t lrck_l;
    uint8_t bclk_div;
    uint8_t adc_osr;
    uint8_t dac_osr;
};

static const Es8311CoeffDiv kEs8311CoeffDiv[] = {
    {12288000, 8000,  0x06, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000, 8000,  0x03, 0x02, 0x03, 0x03, 0x00, 0x05, 0xff, 0x18, 0x10, 0x20},
    {16384000, 8000,  0x08, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {8192000,  8000,  0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,  8000,  0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {4096000,  8000,  0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,  8000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2048000,  8000,  0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000,  8000,  0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1024000,  8000,  0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    {11289600, 11025, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {5644800,  11025, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2822400,  11025, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1411200,  11025, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    {12288000, 12000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,  12000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,  12000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000,  12000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    {12288000, 16000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {18432000, 16000, 0x03, 0x02, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x20},
    {16384000, 16000, 0x04, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {8192000,  16000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {6144000,  16000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {4096000,  16000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {3072000,  16000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {2048000,  16000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1536000,  16000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},
    {1024000,  16000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x20},

    {11289600, 22050, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800,  22050, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400,  22050, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200,  22050, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    {12288000, 24000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 24000, 0x03, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000,  24000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000,  24000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000,  24000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    {12288000, 32000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 32000, 0x03, 0x04, 0x03, 0x03, 0x00, 0x02, 0xff, 0x0c, 0x10, 0x10},
    {16384000, 32000, 0x02, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000,  32000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000,  32000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {4096000,  32000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000,  32000, 0x03, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2048000,  32000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000,  32000, 0x03, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
    {1024000,  32000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    {11289600, 44100, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800,  44100, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400,  44100, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200,  44100, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    {12288000, 48000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 48000, 0x03, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000,  48000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000,  48000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000,  48000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},

    {12288000, 64000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 64000, 0x03, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {16384000, 64000, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {8192000,  64000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000,  64000, 0x01, 0x04, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {4096000,  64000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000,  64000, 0x01, 0x08, 0x03, 0x03, 0x01, 0x01, 0x7f, 0x06, 0x10, 0x10},
    {2048000,  64000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000,  64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0xbf, 0x03, 0x18, 0x18},
    {1024000,  64000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},

    {11289600, 88200, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {5644800,  88200, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {2822400,  88200, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1411200,  88200, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},

    {24576000, 96000, 0x02, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {12288000, 96000, 0x01, 0x02, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {18432000, 96000, 0x03, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {6144000,  96000, 0x01, 0x04, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {3072000,  96000, 0x01, 0x08, 0x01, 0x01, 0x00, 0x00, 0xff, 0x04, 0x10, 0x10},
    {1536000,  96000, 0x01, 0x08, 0x01, 0x01, 0x01, 0x00, 0x7f, 0x02, 0x10, 0x10},
};

bool s_codecInitialized = false;
i2c_master_dev_handle_t s_codecDevHandle = nullptr;

esp_err_t es8311WriteReg(uint8_t reg, uint8_t value) {
    if (s_codecDevHandle == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    uint8_t buf[2] = {reg, value};
    return i2c_master_transmit(s_codecDevHandle, buf, sizeof(buf), 100);
}

esp_err_t es8311ReadReg(uint8_t reg, uint8_t* value) {
    if (s_codecDevHandle == nullptr || value == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2c_master_transmit_receive(s_codecDevHandle, &reg, 1, value, 1, 100);
}

esp_err_t es8311WriteRegChecked(uint8_t reg, uint8_t value) {
    esp_err_t err = es8311WriteReg(reg, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 write reg 0x%02X failed: %s", reg, esp_err_to_name(err));
    }
    return err;
}

esp_err_t es8311ReadRegChecked(uint8_t reg, uint8_t* value) {
    esp_err_t err = es8311ReadReg(reg, value);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "ES8311 read reg 0x%02X failed: %s", reg, esp_err_to_name(err));
    }
    return err;
}

int es8311FindCoeff(uint32_t mclk, uint32_t rate) {
    for (size_t i = 0; i < (sizeof(kEs8311CoeffDiv) / sizeof(kEs8311CoeffDiv[0])); ++i) {
        if (kEs8311CoeffDiv[i].rate == rate && kEs8311CoeffDiv[i].mclk == mclk) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

esp_err_t es8311ConfigSample(uint32_t sampleRate) {
    const uint32_t mclkFreq = sampleRate * ES8311_MCLK_DIV;
    const int coeff = es8311FindCoeff(mclkFreq, sampleRate);
    if (coeff < 0) {
        ESP_LOGE(TAG, "ES8311 no coeff for %luHz with MCLK %luHz",
                 static_cast<unsigned long>(sampleRate),
                 static_cast<unsigned long>(mclkFreq));
        return ESP_ERR_NOT_SUPPORTED;
    }

    uint8_t regv = 0;
    esp_err_t err = es8311ReadRegChecked(ES8311_REG02_CLK_MANAGER, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= 0x07;
    regv |= static_cast<uint8_t>((kEs8311CoeffDiv[coeff].pre_div - 1) << 5);
    uint8_t datmp = 0;
    switch (kEs8311CoeffDiv[coeff].pre_multi) {
        case 1:
            datmp = 0;
            break;
        case 2:
            datmp = 1;
            break;
        case 4:
            datmp = 2;
            break;
        case 8:
            datmp = 3;
            break;
        default:
            datmp = 0;
            break;
    }
    regv |= static_cast<uint8_t>(datmp << 3);
    err = es8311WriteRegChecked(ES8311_REG02_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    regv = 0x00;
    regv |= static_cast<uint8_t>((kEs8311CoeffDiv[coeff].adc_div - 1) << 4);
    regv |= static_cast<uint8_t>((kEs8311CoeffDiv[coeff].dac_div - 1) << 0);
    err = es8311WriteRegChecked(ES8311_REG05_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311ReadRegChecked(ES8311_REG03_CLK_MANAGER, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= 0x80;
    regv |= static_cast<uint8_t>(kEs8311CoeffDiv[coeff].fs_mode << 6);
    regv |= static_cast<uint8_t>(kEs8311CoeffDiv[coeff].adc_osr << 0);
    err = es8311WriteRegChecked(ES8311_REG03_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311ReadRegChecked(ES8311_REG04_CLK_MANAGER, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= 0x80;
    regv |= static_cast<uint8_t>(kEs8311CoeffDiv[coeff].dac_osr << 0);
    err = es8311WriteRegChecked(ES8311_REG04_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311ReadRegChecked(ES8311_REG07_CLK_MANAGER, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= 0xC0;
    regv |= static_cast<uint8_t>(kEs8311CoeffDiv[coeff].lrck_h << 0);
    err = es8311WriteRegChecked(ES8311_REG07_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    regv = static_cast<uint8_t>(kEs8311CoeffDiv[coeff].lrck_l);
    err = es8311WriteRegChecked(ES8311_REG08_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311ReadRegChecked(ES8311_REG06_CLK_MANAGER, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= 0xE0;
    if (kEs8311CoeffDiv[coeff].bclk_div < 19) {
        regv |= static_cast<uint8_t>((kEs8311CoeffDiv[coeff].bclk_div - 1) << 0);
    } else {
        regv |= static_cast<uint8_t>((kEs8311CoeffDiv[coeff].bclk_div) << 0);
    }
    return es8311WriteRegChecked(ES8311_REG06_CLK_MANAGER, regv);
}

esp_err_t es8311ConfigFormatI2S() {
    uint8_t dac_iface = 0;
    uint8_t adc_iface = 0;
    esp_err_t err = es8311ReadRegChecked(ES8311_REG09_SDP_IN, &dac_iface);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311ReadRegChecked(ES8311_REG0A_SDP_OUT, &adc_iface);
    if (err != ESP_OK) {
        return err;
    }
    dac_iface &= 0xFC;
    adc_iface &= 0xFC;
    err = es8311WriteRegChecked(ES8311_REG09_SDP_IN, dac_iface);
    if (err != ESP_OK) {
        return err;
    }
    return es8311WriteRegChecked(ES8311_REG0A_SDP_OUT, adc_iface);
}

esp_err_t es8311SetBitsPerSample(uint8_t bits) {
    uint8_t dac_iface = 0;
    uint8_t adc_iface = 0;
    esp_err_t err = es8311ReadRegChecked(ES8311_REG09_SDP_IN, &dac_iface);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311ReadRegChecked(ES8311_REG0A_SDP_OUT, &adc_iface);
    if (err != ESP_OK) {
        return err;
    }
    dac_iface &= static_cast<uint8_t>(~0x1C);
    adc_iface &= static_cast<uint8_t>(~0x1C);
    switch (bits) {
        case 16:
        default:
            dac_iface |= 0x0C;
            adc_iface |= 0x0C;
            break;
        case 24:
            break;
        case 32:
            dac_iface |= 0x10;
            adc_iface |= 0x10;
            break;
    }
    err = es8311WriteRegChecked(ES8311_REG09_SDP_IN, dac_iface);
    if (err != ESP_OK) {
        return err;
    }
    return es8311WriteRegChecked(ES8311_REG0A_SDP_OUT, adc_iface);
}

esp_err_t es8311StartDacOnly() {
    uint8_t regv = 0x80;
    esp_err_t err = es8311WriteRegChecked(ES8311_REG00_RESET, regv);
    if (err != ESP_OK) {
        return err;
    }

    regv = 0x3F;
    regv &= 0x7F; // use MCLK
    regv &= static_cast<uint8_t>(~0x40); // not inverted
    err = es8311WriteRegChecked(ES8311_REG01_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t dac_iface = 0;
    uint8_t adc_iface = 0;
    err = es8311ReadRegChecked(ES8311_REG09_SDP_IN, &dac_iface);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311ReadRegChecked(ES8311_REG0A_SDP_OUT, &adc_iface);
    if (err != ESP_OK) {
        return err;
    }
    dac_iface &= 0xBF;
    adc_iface &= 0xBF;
    adc_iface |= 0x40; // disable ADC
    dac_iface &= static_cast<uint8_t>(~0x40); // enable DAC
    err = es8311WriteRegChecked(ES8311_REG09_SDP_IN, dac_iface);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG0A_SDP_OUT, adc_iface);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311WriteRegChecked(ES8311_REG17_ADC, 0xBF);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG0E_SYSTEM, 0x02);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG12_SYSTEM, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG14_SYSTEM, 0x1A);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG0D_SYSTEM, 0x01);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG15_ADC, 0x40);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG37_DAC, 0x08);
    if (err != ESP_OK) {
        return err;
    }
    return es8311WriteRegChecked(ES8311_REG45_GP, 0x00);
}

esp_err_t es8311SetDacVolume(uint8_t volume) {
    return es8311WriteRegChecked(ES8311_REG32_DAC, volume);
}

esp_err_t es8311UnmuteDac() {
    uint8_t regv = 0;
    esp_err_t err = es8311ReadRegChecked(ES8311_REG31_DAC, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= static_cast<uint8_t>(~0x60);
    return es8311WriteRegChecked(ES8311_REG31_DAC, regv);
}

esp_err_t es8311EnsureDevice() {
    if (s_codecInitialized) {
        return ESP_OK;
    }

    i2c_master_bus_handle_t busHandle = hal_i2c_get_bus_handle();
    if (busHandle == nullptr) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Add ES8311 device to I2C bus
    i2c_device_config_t devCfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = ES8311_ADDR,
        .scl_speed_hz = 100000,
        .scl_wait_us = 0,
        .flags = {
            .disable_ack_check = false,
        },
    };
    esp_err_t err = i2c_master_bus_add_device(busHandle, &devCfg, &s_codecDevHandle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ES8311 to I2C bus: %s", esp_err_to_name(err));
        return err;
    }

    // Check chip ID
    uint8_t chipId = 0;
    err = es8311ReadRegChecked(ES8311_REG45_CHIPID, &chipId);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 not detected (read failed): %s", esp_err_to_name(err));
        i2c_master_bus_rm_device(s_codecDevHandle);
        s_codecDevHandle = nullptr;
        return err;
    }
    ESP_LOGI(TAG, "ES8311 chip ID: 0x%02X", chipId);

    s_codecInitialized = true;
    return ESP_OK;
}

esp_err_t es8311Open() {
    esp_err_t err = es8311WriteRegChecked(ES8311_REG44_DACGPF, 0x08);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG44_DACGPF, 0x08);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311WriteRegChecked(ES8311_REG01_CLK_MANAGER, 0x30);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG02_CLK_MANAGER, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG03_CLK_MANAGER, 0x10);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG16_ADC, 0x24);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG04_CLK_MANAGER, 0x10);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG05_CLK_MANAGER, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG0B_SYSTEM, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG0C_SYSTEM, 0x00);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG10_SYSTEM, 0x1F);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG11_SYSTEM, 0x7F);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG00_RESET, 0x80);
    if (err != ESP_OK) {
        return err;
    }

    uint8_t regv = 0;
    err = es8311ReadRegChecked(ES8311_REG00_RESET, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= static_cast<uint8_t>(~0x40); // slave mode
    err = es8311WriteRegChecked(ES8311_REG00_RESET, regv);
    if (err != ESP_OK) {
        return err;
    }

    regv = 0x3F;
    regv &= 0x7F; // use MCLK
    regv &= static_cast<uint8_t>(~0x40); // no invert
    err = es8311WriteRegChecked(ES8311_REG01_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311ReadRegChecked(ES8311_REG06_CLK_MANAGER, &regv);
    if (err != ESP_OK) {
        return err;
    }
    regv &= static_cast<uint8_t>(~0x20); // sclk not inverted
    err = es8311WriteRegChecked(ES8311_REG06_CLK_MANAGER, regv);
    if (err != ESP_OK) {
        return err;
    }

    err = es8311WriteRegChecked(ES8311_REG13_SYSTEM, 0x10);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG1B_ADC, 0x0A);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311WriteRegChecked(ES8311_REG1C_ADC, 0x6A);
    if (err != ESP_OK) {
        return err;
    }
    return es8311WriteRegChecked(ES8311_REG44_DACGPF, 0x58);
}

esp_err_t es8311Configure(uint32_t sampleRate, uint8_t bitsPerSample) {
    esp_err_t err = es8311EnsureDevice();
    if (err != ESP_OK) {
        return err;
    }

    err = es8311Open();
    if (err != ESP_OK) {
        return err;
    }

    err = es8311SetBitsPerSample(bitsPerSample);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311ConfigFormatI2S();
    if (err != ESP_OK) {
        return err;
    }
    err = es8311ConfigSample(sampleRate);
    if (err != ESP_OK) {
        return err;
    }
    err = es8311StartDacOnly();
    if (err != ESP_OK) {
        return err;
    }
    err = es8311SetDacVolume(0xC0);
    if (err != ESP_OK) {
        return err;
    }
    return es8311UnmuteDac();
}

}  // namespace

Speaker::Speaker()
    : txChannel(nullptr),
      initialized(false),
      currentSampleRate(0),
      currentChannels(0),
      currentBitsPerSample(0),
      softwareVolume(0.25f) {
}

Speaker::~Speaker() {
    deinitialize();
}

esp_err_t Speaker::initialize(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample) {
    if (initialized &&
        currentSampleRate == sampleRate &&
        currentChannels == channels &&
        currentBitsPerSample == bitsPerSample) {
        return ESP_OK;
    }

    deinitialize();

    // Bring up I2S first so MCLK/BCLK/LRCK are present for the codec init.
    esp_err_t err = configure(sampleRate, channels, bitsPerSample);
    if (err != ESP_OK) {
        return err;
    }

    // Give the clocks a moment to stabilize before touching the codec.
    vTaskDelay(pdMS_TO_TICKS(10));

    err = es8311Configure(sampleRate, bitsPerSample);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "ES8311 codec init failed: %s (continuing without codec)", esp_err_to_name(err));
    }

    err = enableAmplifier();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Speaker amplifier enable failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Speaker amplifier enabled");
    }

    initialized = true;
    currentSampleRate = sampleRate;
    currentChannels = channels;
    currentBitsPerSample = bitsPerSample;

    ESP_LOGI(TAG, "Speaker initialized (%lu Hz, %u ch, %u-bit)",
             static_cast<unsigned long>(sampleRate),
             static_cast<unsigned>(channels),
             static_cast<unsigned>(bitsPerSample));
    return ESP_OK;
}

void Speaker::deinitialize() {
    if (txChannel != nullptr) {
        i2s_channel_disable(txChannel);
        i2s_del_channel(txChannel);
        txChannel = nullptr;
    }
    initialized = false;
    currentSampleRate = 0;
    currentChannels = 0;
    currentBitsPerSample = 0;
}

esp_err_t Speaker::write(const uint8_t* data, size_t len, size_t* written, uint32_t timeoutMs) {
    if (!initialized || txChannel == nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    if (data == nullptr || len == 0) {
        if (written != nullptr) {
            *written = 0;
        }
        return ESP_OK;
    }

    size_t bytesWritten = 0;
    esp_err_t err = ESP_OK;

    if (softwareVolume < 0.999f && currentBitsPerSample == 16) {
        const size_t sampleCount = len / sizeof(int16_t);
        std::vector<int16_t> scaled(sampleCount);

        auto* src = reinterpret_cast<const int16_t*>(data);
        for (size_t i = 0; i < sampleCount; ++i) {
            const float scaledValue = static_cast<float>(src[i]) * softwareVolume;
            const float clamped = std::max(-32768.0f, std::min(32767.0f, scaledValue));
            scaled[i] = static_cast<int16_t>(std::lround(clamped));
        }

        err = i2s_channel_write(txChannel,
                                scaled.data(),
                                sampleCount * sizeof(int16_t),
                                &bytesWritten,
                                pdMS_TO_TICKS(timeoutMs));
    } else {
        err = i2s_channel_write(txChannel, data, len, &bytesWritten, pdMS_TO_TICKS(timeoutMs));
    }

    if (written != nullptr) {
        *written = bytesWritten;
    }
    return err;
}

void Speaker::setSoftwareVolume(float volume) {
    softwareVolume = std::max(MIN_VOLUME, std::min(MAX_VOLUME, volume));
}

esp_err_t Speaker::enableAmplifier() {
    return hal_exio_set_pin_output_level(AUDIO_PA_EN_EXIO_BIT, true);
}

esp_err_t Speaker::configure(uint32_t sampleRate, uint8_t channels, uint8_t bitsPerSample) {
    i2s_chan_config_t chanCfg = I2S_CHANNEL_DEFAULT_CONFIG(AUDIO_I2S_PORT, I2S_ROLE_MASTER);
    chanCfg.auto_clear = true;
    esp_err_t err = i2s_new_channel(&chanCfg, &txChannel, nullptr);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_new_channel failed: %s", esp_err_to_name(err));
        return err;
    }

    i2s_data_bit_width_t bitWidth = I2S_DATA_BIT_WIDTH_16BIT;
    switch (bitsPerSample) {
        case 16:
            bitWidth = I2S_DATA_BIT_WIDTH_16BIT;
            break;
        case 24:
            bitWidth = I2S_DATA_BIT_WIDTH_24BIT;
            break;
        case 32:
            bitWidth = I2S_DATA_BIT_WIDTH_32BIT;
            break;
        default:
            ESP_LOGE(TAG, "Unsupported bit depth: %u", static_cast<unsigned>(bitsPerSample));
            return ESP_ERR_INVALID_ARG;
    }

    i2s_slot_mode_t slotMode = (channels == 1) ? I2S_SLOT_MODE_MONO : I2S_SLOT_MODE_STEREO;
    i2s_std_clk_config_t clkCfg = I2S_STD_CLK_DEFAULT_CONFIG(sampleRate);
    clkCfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    clkCfg.ext_clk_freq_hz = 0;
    const gpio_num_t mclkPin = static_cast<gpio_num_t>((AUDIO_I2S_MCLK_PIN >= 0) ? AUDIO_I2S_MCLK_PIN : I2S_GPIO_UNUSED);
    const gpio_num_t bclkPin = static_cast<gpio_num_t>((AUDIO_I2S_BCLK_PIN >= 0) ? AUDIO_I2S_BCLK_PIN : I2S_GPIO_UNUSED);
    const gpio_num_t wsPin = static_cast<gpio_num_t>((AUDIO_I2S_WS_PIN >= 0) ? AUDIO_I2S_WS_PIN : I2S_GPIO_UNUSED);
    const gpio_num_t doutPin = static_cast<gpio_num_t>((AUDIO_I2S_DOUT_PIN >= 0) ? AUDIO_I2S_DOUT_PIN : I2S_GPIO_UNUSED);

    i2s_std_config_t stdCfg = {
        .clk_cfg = clkCfg,
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bitWidth, slotMode),
        .gpio_cfg = {
            .mclk = mclkPin,
            .bclk = bclkPin,
            .ws = wsPin,
            .dout = doutPin,
            .din = static_cast<gpio_num_t>(I2S_GPIO_UNUSED),
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(txChannel, &stdCfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_init_std_mode failed: %s", esp_err_to_name(err));
        i2s_del_channel(txChannel);
        txChannel = nullptr;
        return err;
    }

    err = i2s_channel_enable(txChannel);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "i2s_channel_enable failed: %s", esp_err_to_name(err));
        i2s_del_channel(txChannel);
        txChannel = nullptr;
        return err;
    }

    return ESP_OK;
}
