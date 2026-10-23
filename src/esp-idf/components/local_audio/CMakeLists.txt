# Local audio component (PERI-11): touch and swipe effects plus the recorder, behind LocalAudio.
idf_component_register(
    SRCS "LocalAudio.cpp"
    INCLUDE_DIRS "."
    REQUIRES media input
    PRIV_REQUIRES esp_timer
)
