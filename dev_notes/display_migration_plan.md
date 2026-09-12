# Display Migration Status

## Goal

Keep the firmware on one display model:

- `LVGL` owns every application screen.
- Panel startup, flushing, brightness, and sleep/wake behavior stay behind display backend classes.
- Screen code never calls panel-driver drawing APIs directly.
- Board-specific display details stay in `esp-idf/components/display`.

## Current State

The app has been migrated away from the previous mixed direct-draw path. The active display flow is now:

1. `main.cpp` creates a display panel backend through `DisplayBackendFactory`.
2. The backend initializes the panel and exposes a flush backend.
3. `main.cpp` initializes `LvglDisplay` once with that flush backend.
4. All screens and controllers receive `DisplayPanelBackend` only for dimensions, brightness, and power hooks.
5. UI rendering is done with LVGL objects.
6. `DisplayController` owns routine LVGL task pumping during active screen updates.

## Completed Work

- Added `DisplayPanelBackend` and `DisplayFlushBackend` abstractions.
- Added an `esp_lcd` QSPI AMOLED panel backend.
- Converted initialization, mode selection, environment info, device info, light, and sound screens to LVGL.
- Removed direct framebuffer helper primitives from the active build.
- Removed the old vendored direct-draw display component from the project tree.
- Removed direct display-library dependencies from `main`, `display`, `ui`, and `controllers` component build files.

## Active Files

- `esp-idf/main/main.cpp`
- `esp-idf/components/display/DisplayBackendFactory.cpp`
- `esp-idf/components/display/DisplayPanelBackend.h`
- `esp-idf/components/display/DisplayFlushBackend.h`
- `esp-idf/components/display/EspLcdDisplayPanelBackend.cpp`
- `esp-idf/components/display/EspLcdSh8601DisplayFlushBackend.cpp`
- `esp-idf/components/display/EspLcdGc9a01DisplayPanelBackend.cpp`
- `esp-idf/components/display/EspLcdGc9a01DisplayFlushBackend.cpp`
- `esp-idf/components/ui/LvglDisplay.cpp`
- `esp-idf/components/ui/screens/InitializationScreen.cpp`
- `esp-idf/components/ui/screens/ModeController.cpp`
- `esp-idf/components/ui/screens/EnvironmentInfoScreen.cpp`
- `esp-idf/components/ui/screens/DeviceInfoScreen.cpp`
- `esp-idf/components/controllers/LightController.cpp`
- `esp-idf/components/controllers/SoundController.cpp`

## Remaining Work

1. Build and hardware-validate the LVGL-only AMOLED path.
2. Confirm QSPI AMOLED init sequences, SPI LCD init sequence, brightness behavior, color order, and flush completion on real hardware.
3. Restore richer LVGL-native visuals for light and sound controls where the first conversion intentionally simplified direct-draw artwork.
4. Add native panel backends for any non-QSPI variants before those variants are expected to build or ship.
5. Run a board validation matrix covering boot splash, screen transitions, touch gestures, knob interaction, sleep/wake, brightness, animations, and extended idle stability.

## Validation Checklist

- Firmware builds for the selected variant.
- Boot splash is visible before the first app screen.
- Environment and device info arcs animate from their configured start angles.
- Touch navigation still returns from sub-screens.
- Brightness fade and display sleep/wake still work.
- No active source file includes or depends on the removed direct-draw display stack.
