#ifndef COMMON_H
#define COMMON_H

enum Mode {
    SOUND,
    LIGHT,
    TEMPERATURE,
    HOME,
    CALIBRATE_ORIENTATION,
    INITIALIZATION,  // New mode for initialization screen
    INFO,        // New mode for info screen
    SLEEP // New sleep mode
};

// Increased to include INFO mode (but not INITIALIZATION as it's not user-selectable)
const int MODE_COUNT = 5; // Total number of selectable modes

#endif