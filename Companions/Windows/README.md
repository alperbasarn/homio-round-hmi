# Qnob Companion (Windows)

Cross-platform Python companion app for the Qnob smart-knob device. Discovers devices on the local network (mDNS) and over BLE, then drives configuration and control over a JSON envelope protocol (TCP or BLE-UART transport).

> **Stack:** Python 3.12 + PySide6 + asyncio. See [`docs/protocol.md`](../../docs/protocol.md) for the wire protocol.

The "Windows" path component is historical; the codebase is cross-platform (macOS / Linux work too — only the PC-audio module is Windows-gated).

## Status

This is the rewrite of the legacy tkinter companion. The Python migration replaces:

- tkinter UI → **PySide6** (Qt 6)
- MQTT-as-transport → **TCP envelope** + **BLE Nordic-UART** (per `docs/protocol.md`)
- pyserial → dropped (USB/BLE replaces it)
- `qnob_config.json` → `pydantic` settings at user-config dir

PC-side audio (`pycaw`) and media-key (`pywin32`) helpers were preserved from the legacy code and live under `qnob_companion.pc`.

## Quick start

### Normal setup (no corporate proxy)

```powershell
cd Companions\Windows
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]
qnob-companion
```

### Corporate proxy / air-gapped setup

The corporate proxy typically blocks large downloads (PySide6 Essentials ~78 MB, Addons ~169 MB).
Use the provided script to **download all wheels while on a hotspot/home network**, then install
them offline back on the corporate network:

```powershell
cd Companions\Windows

# Step 1 – while connected to a network without the intercepting proxy:
.\scripts\install-deps.ps1 -Download -WheelDir C:\qnob_wheels

# Step 2 – install from the saved wheels (works on any network):
.\scripts\install-deps.ps1 -Install -WheelDir C:\qnob_wheels
```

Or combine both steps in one go (requires unrestricted internet access):

```powershell
.\scripts\install-deps.ps1 -WheelDir C:\qnob_wheels
```

#### Manual install order (if the script is unavailable)

```powershell
python -m venv .venv
.\.venv\Scripts\Activate.ps1

# 1. Download all wheels to a local directory (on unrestricted network)
pip download shiboken6 PySide6_Essentials PySide6_Addons qasync zeroconf bleak platformdirs `
    keyring "jaraco.context" "jaraco.functools" "jaraco.classes" `
    importlib_metadata pywin32-ctypes annotated-types typing-inspection `
    -d C:\qnob_wheels\

# 2. Install from local wheels (no network required)
pip install --no-index --find-links C:\qnob_wheels\ `
    shiboken6 PySide6_Essentials PySide6_Addons qasync zeroconf bleak platformdirs `
    keyring "jaraco.context" "jaraco.functools" "jaraco.classes" `
    importlib_metadata pywin32-ctypes annotated-types typing-inspection

# 3. Install the package itself
pip install -e .[dev] --no-deps
```

## Development

```powershell
pytest                  # tests
ruff check src tests    # lint
mypy src                # type-check
```

## Layout

```
Companions/Windows/
├── pyproject.toml
├── README.md
├── src/qnob_companion/
│   ├── app/         # Entry point, settings, logging
│   ├── protocol/    # Envelope dataclasses (matches docs/protocol.md)
│   ├── discovery/   # mDNS + BLE scanner (#61 B2)
│   ├── transport/   # TCP + BLE clients + DeviceClient (#62 B3)
│   ├── pairing/     # Token wizard + Credential Manager (#65 B4)
│   ├── ui/          # PySide6 windows and pages
│   └── pc/          # PC-side helpers (audio, media keys)
└── tests/
```

## Configuration & data

| Path | Purpose |
|---|---|
| `%LOCALAPPDATA%\Qnob\settings.json` | App settings (paired devices, transport prefs) |
| Windows Credential Manager (`Qnob:<mac>`) | Per-device pairing tokens (via `keyring`) |
| `%LOCALAPPDATA%\Qnob\Logs\companion.log` | Rotating log file |
