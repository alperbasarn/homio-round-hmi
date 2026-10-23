# Hardware Ref

Reference hardware PDFs for the ESP screen project.

## Variant Config Notes

- `variant_overview.csv` documents each supported board's pin mapping, hardware presence, feature enablement, bus usage, and current display/touch orientation defaults.
- Per-variant display and touch orientation are configured in `src/esp-idf/components/board_hal/Kconfig`.
- The Kconfig choices are exported as board-wide macros in `src/esp-idf/components/board_hal/hal_config.h` and applied centrally by `src/esp-idf/components/ui/LvglDisplay.cpp` and `src/esp-idf/components/touch_panel/TouchPanel.cpp`.
- Variant builds write their generated `sdkconfig.<variant>` files next to their build directories under `helpers/` (ignored), and those pick up these defaults.

## Included Files

- `ESP32-C6-Touch-AMOLED-1.43_Rev1.3.pdf`
- `ESP32-S3-Touch-AMOLED-1.75.pdf` (the bench board)
- `ESP32-S3-Touch-LCD-1.28-Sch.pdf`
