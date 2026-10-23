# Media Component - SD card and audio capture/playback helpers
idf_component_register(
    SRCS
        "SDCard.cpp"
        "Speaker.cpp"
        "Microphone.cpp"
        "Mp3Decoder.cpp"
        "SoundPlayer.cpp"
        "SoundRecorder.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        board_hal
        fatfs
        sdmmc
        esp_driver_sdspi
        esp_driver_spi
        esp_driver_i2s
        esp_timer
)
