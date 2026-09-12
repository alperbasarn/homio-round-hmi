# Active LVGL Screen Flow

## Purpose

Record the current display ownership path so future work keeps the firmware on a single LVGL-based UI model.

## Runtime Flow

1. `main.cpp` asks `DisplayBackendFactory` for the selected panel backend.
2. The panel backend initializes the display hardware.
3. `main.cpp` initializes `LvglDisplay` with the backend's `DisplayFlushBackend`.
4. `main.cpp` creates the initialization, mode, environment, device, light, and sound screens.
5. Each screen renders through LVGL widgets and styles.
6. `DisplayController::update()` calls the active screen update method.
7. `DisplayController::update()` runs routine `LvglDisplay::taskHandler()` for LVGL screen refresh.
8. `LvglDisplay` flushes rendered areas through the display backend abstraction.

## Relevant Files

- `esp-idf/main/main.cpp`
- `esp-idf/components/display/DisplayController.cpp`
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

## Backend Selection

Display backend selection lives in:

- `esp-idf/components/display/DisplayBackendFactory.h`
- `esp-idf/components/display/DisplayBackendFactory.cpp`

Current behavior:

- AMOLED variants select an `esp_lcd` QSPI panel backend
- the LCD 1.28 variant selects an `esp_lcd` SPI GC9A01 backend
- screen code receives only `DisplayPanelBackend`
- LVGL receives only `DisplayFlushBackend`
- panel-specific initialization, brightness, and flush behavior stay in the display component

## Lifecycle Rules

- Initialize `LvglDisplay` once during app startup.
- Screens must not initialize LVGL themselves.
- Screens show and hide their own root objects instead of clearing the framebuffer.
- Routine LVGL pumping belongs to `DisplayController`.
- Extra `LvglDisplay::taskHandler()` calls are allowed only for explicit immediate-render cases such as boot splash refresh.

## Remaining Validation

- AMOLED hardware boot and splash visibility
- color order and pixel packing
- arc animation direction and angle mapping
- touch and navigation consistency
- sleep/wake and brightness behavior
