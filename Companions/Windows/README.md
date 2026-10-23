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

### Easiest — one-click launcher

Right-click `launch.ps1` → **Run with PowerShell**.  
On first run it creates the `.venv` and installs all deps automatically.
Subsequent runs just open the app window.

```powershell
.\launch.ps1
```

> Requires **Python 3.12** installed from [python.org](https://python.org/downloads).  
> If you have Anaconda/conda as your system Python, the launcher will still pick up
> Python 3.12 via the `py -3.12` launcher — no PATH changes needed.

### Manual setup (dev / testing)

```powershell
cd Companions\Windows
py -3.12 -m venv .venv
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

## Development

```powershell
pytest                  # run tests
ruff check src tests    # lint
mypy src                # type-check
```

## Building a distributable

```powershell
pip install -e .[build]                          # adds pyinstaller
pyinstaller qnob_companion.spec --clean          # → dist/qnob-companion/
.\scripts\build_installer.ps1                    # also builds MSIX if Windows SDK present
```

Add PNG assets to `packaging/Assets/` before building MSIX (see `packaging/AppxManifest.xml` for required sizes).

## Layout

```
Companions/Windows/
├── launch.ps1               # one-click launcher (creates venv on first run)
├── pyproject.toml
├── qnob_companion.spec      # PyInstaller build spec
├── packaging/
│   ├── AppxManifest.xml     # MSIX manifest
│   └── Assets/              # PNG icons (add before building MSIX)
├── scripts/
│   ├── install-deps.ps1     # offline/proxy wheel download+install
│   └── build_installer.ps1  # PyInstaller + MSIX packaging
├── src/qnob_companion/
│   ├── app/         # Entry point, settings, logging, auto-updater
│   ├── protocol/    # Envelope dataclasses (matches docs/protocol.md)
│   ├── discovery/   # mDNS + BLE scanner
│   ├── transport/   # TCP + BLE transports + DeviceClient
│   ├── pairing/     # Token wizard + Credential Manager
│   ├── ota/         # Chunked firmware upload (otaBegin/otaChunk)
│   ├── ui/          # PySide6 windows, pages, and device-detail tabs
│   └── pc/          # PC-side helpers (audio, media keys)
└── tests/
```

## Configuration & data

| Path | Purpose |
|---|---|
| `%LOCALAPPDATA%\Qnob\settings.json` | App settings (paired devices, transport prefs) |
| Windows Credential Manager (`Qnob:<mac>`) | Per-device pairing tokens (via `keyring`) |
| `%LOCALAPPDATA%\Qnob\Logs\companion.log` | Rotating log file |
