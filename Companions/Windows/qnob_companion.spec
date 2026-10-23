# -*- mode: python ; coding: utf-8 -*-
#
# PyInstaller spec for Qnob Companion (one-dir mode, Windows x64).
#
# Build with:
#   cd Companions/Windows
#   pyinstaller qnob_companion.spec --clean
#
# Output: dist/qnob-companion/qnob-companion.exe (+ DLLs and Qt plugins)

import sys
from pathlib import Path

block_cipher = None

# ---------------------------------------------------------------------------
# Analysis
# ---------------------------------------------------------------------------

a = Analysis(
    [str(Path("src") / "qnob_companion" / "app" / "main.py")],
    pathex=[str(Path("src"))],
    binaries=[],
    datas=[],
    hiddenimports=[
        # PySide6 — platform and image plugins that PyInstaller misses
        "PySide6.QtSvg",
        "PySide6.QtSvgWidgets",
        "PySide6.QtNetwork",
        "PySide6.QtPrintSupport",
        # bleak WinRT backend (selected at runtime via importlib)
        "bleak.backends.winrt",
        "bleak.backends.winrt.client",
        "bleak.backends.winrt.scanner",
        "bleak.backends.winrt.service",
        "bleak.backends.winrt.characteristic",
        "bleak.backends.winrt.descriptor",
        "bleak.backends.winrt.utils",
        # zeroconf dynamic sub-modules
        "zeroconf._utils",
        "zeroconf._utils.ipaddress",
        "zeroconf._utils.net",
        "zeroconf._utils.time",
        "zeroconf._dns",
        "zeroconf._services",
        "zeroconf._services.browser",
        "zeroconf._services.info",
        # keyring Windows Credential Manager backend
        "keyring.backends.Windows",
        "keyring.backends._Windows_cffi",
        # pywin32 — timezone data required at import
        "win32timezone",
        # asyncio Windows event-loop policy
        "asyncio.windows_events",
        "asyncio.windows_utils",
        # pydantic V2 core
        "pydantic.v1",
        "pydantic_core",
    ],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[
        "tkinter",
        "_tkinter",
        "test",
        "unittest",
        "doctest",
        "pdb",
        "difflib",
        "ftplib",
        "imaplib",
        "smtplib",
        "poplib",
        "nntplib",
        "telnetlib",
    ],
    win_no_prefer_redirects=False,
    win_private_assemblies=False,
    cipher=block_cipher,
    noarchive=False,
)

# ---------------------------------------------------------------------------
# PYZ archive
# ---------------------------------------------------------------------------

pyz = PYZ(a.pure, a.zipped_data, cipher=block_cipher)

# ---------------------------------------------------------------------------
# EXE stub (one-dir mode: no binaries/datas here; they go in COLLECT)
# ---------------------------------------------------------------------------

exe = EXE(
    pyz,
    a.scripts,
    [],
    exclude_binaries=True,  # one-dir: binaries collected separately
    name="qnob-companion",
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    console=False,           # no console window (pure GUI app)
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
    # icon="packaging/Assets/app.ico",  # uncomment once icon is ready
)

# ---------------------------------------------------------------------------
# COLLECT — assembles the final dist/qnob-companion/ directory
# ---------------------------------------------------------------------------

coll = COLLECT(
    exe,
    a.binaries,
    a.zipfiles,
    a.datas,
    strip=False,
    upx=True,
    upx_exclude=["vcruntime140.dll", "msvcp140.dll"],
    name="qnob-companion",
)
