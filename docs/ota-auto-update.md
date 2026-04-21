# OTA Auto Update Feature

## Overview

This project now supports a tag-driven OTA release pipeline and on-device software update flow.

The end-to-end behavior is:

1. A git tag matching release-x.y.z.t is pushed.
2. CI builds firmware for all supported hardware variants.
3. OTA binaries and JSON manifests are published to GitHub Pages.
4. Device firmware reads the variant-specific manifest.
5. The Device Info page provides an Update Software button to trigger OTA.

## Implemented Components

### Release Pipeline

- Workflow: [.github/workflows/release-ota.yml](../.github/workflows/release-ota.yml)
- Trigger: push tags matching release-*
- Build matrix variants:
  - esp32s3_lcd128
  - esp32s3_amoled175
  - esp32c6_amoled143

The workflow:

1. Checks out repository.
2. Builds each variant via build-variant.sh.
3. Sets PROJECT_VER from the pushed tag suffix.
4. Collects binaries per variant.
5. Generates OTA website payload (binaries + manifests).
6. Deploys to GitHub Pages.

### Build Scripts

- Linux/CI build script: [build-variant.sh](../build-variant.sh)
- Windows build script: [build-variant.ps1](../build-variant.ps1)

Enhancements added:

- Optional PROJECT_VER injection for release builds.
- Configurable ESP-IDF export path in shell build.

### OTA Site Generator

- Script: [scripts/generate_ota_site.py](../scripts/generate_ota_site.py)

This script creates the static OTA payload used by devices:

- Versioned binaries per release tag
- Per-variant latest manifest
- Per-release immutable manifests
- Latest index document

## Hosting Layout

Published under:

- Base: https://alperbasarn.github.io/homio-round-hmi/ota

Generated structure:

- ota/latest/index.json
- ota/latest/<variant>.json
- ota/releases/<release-tag>/<variant>.json
- ota/releases/<release-tag>/<variant>/qnob-screen.bin

## Manifest Contract

Each variant manifest contains:

- channel
- release_tag
- version
- variant
- board_name
- chip
- published_at
- binary_url
- sha256
- size_bytes

Example:

```json
{
  "channel": "stable",
  "release_tag": "release-1.2.3.4",
  "version": "1.2.3.4",
  "variant": "esp32s3_amoled175",
  "board_name": "ESP32-S3 Touch AMOLED 1.75",
  "chip": "esp32s3",
  "published_at": "2026-04-21T10:30:00Z",
  "binary_url": "https://alperbasarn.github.io/homio-round-hmi/ota/releases/release-1.2.3.4/esp32s3_amoled175/qnob-screen.bin",
  "sha256": "...",
  "size_bytes": 1638400
}
```

## Firmware Integration

### Variant Identity

Variant IDs are defined in:

- [esp-idf/components/board_hal/hal_config.h](../esp-idf/components/board_hal/hal_config.h)

Used IDs:

- esp32s3_lcd128
- esp32s3_amoled175
- esp32c6_amoled143

### OTA Manager

Files:

- [esp-idf/components/ota_manager/OTAManager.h](../esp-idf/components/ota_manager/OTAManager.h)
- [esp-idf/components/ota_manager/OTAManager.cpp](../esp-idf/components/ota_manager/OTAManager.cpp)

New capabilities:

- Manifest URL configuration
- Device variant ID configuration
- Release metadata fetch and parse
- Version comparison
- Update availability state reporting
- Release update start method

### Main Application Wiring

- [esp-idf/main/main.cpp](../esp-idf/main/main.cpp)

Main now:

- configures OTAManager with variant ID
- configures manifest URL
- exposes OTA release status callback to Device Info UI
- binds Update Software action callback to OTAManager

### Device Info UI

Files:

- [esp-idf/components/ui/screens/DeviceInfoScreen.h](../esp-idf/components/ui/screens/DeviceInfoScreen.h)
- [esp-idf/components/ui/screens/DeviceInfoScreen.cpp](../esp-idf/components/ui/screens/DeviceInfoScreen.cpp)

UI additions:

- software version row
- update status text
- Update Software button
- busy state handling

Navigation adjustment:

- Swipe down remains back navigation.
- Generic tap-to-go-back was removed to avoid conflict with the new update button.

## Release Process

Use this process for each OTA release:

1. Ensure changes are merged and validated.
2. Create and push a tag in format release-x.y.z.t.
3. Wait for Release OTA workflow to complete.
4. Verify files are published on GitHub Pages.
5. On device, open Device Info and tap Update Software.

## Validation Checklist

After publishing a release:

- workflow ran successfully for all variants
- latest variant manifests are reachable
- manifest variant matches device variant
- device shows current and available version correctly
- OTA starts and completes
- device reboots into updated firmware

## Troubleshooting

### No update available

Check:

- manifest URL is reachable
- device variant matches manifest variant
- version in manifest is greater than current running version

### OTA start fails

Check:

- Wi-Fi connected
- OTAManager configured
- manifest and binary URLs accessible from network
- binary file exists at published URL

### Manifest fetch fails

Check:

- GitHub Pages deployment completed
- JSON format is valid
- firmware has correct base URL

## Security Notes

Current implementation is designed for hosted OTA via GitHub Pages over HTTPS.
For production hardening, consider:

- certificate pinning
- signed manifests
- rollout channels (stable/beta/dev)
- progressive rollout controls

## Future Enhancements

Recommended next steps:

- add explicit Check for Updates action (separate from install)
- display release notes in UI
- expose last update result in diagnostics
- make OTA base URL configurable via NVS or commissioning flow
