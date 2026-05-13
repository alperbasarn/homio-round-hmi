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

```powershell
cd Companions\Windows
python -m venv .venv
.\.venv\Scripts\Activate.ps1
pip install -e .[dev]
qnob-companion
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
