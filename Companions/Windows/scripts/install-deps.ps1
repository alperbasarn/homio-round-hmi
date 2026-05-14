<#
.SYNOPSIS
    Download and/or install all Qnob Companion Python dependencies.

.DESCRIPTION
    Handles the corporate-proxy / air-gapped workflow:
      - Use -Download to fetch all wheels to a local directory (needs unrestricted internet).
      - Use -Install  to install from a previously downloaded directory (works offline).
      - Omit both flags to do download + install in one step.

.PARAMETER WheelDir
    Local directory used to store downloaded wheel files.
    Defaults to: C:\qnob_wheels

.PARAMETER VenvDir
    Path to the Python virtual environment.
    Defaults to: .venv  (relative to the script's parent folder)

.PARAMETER Download
    Download wheels only; do not install.

.PARAMETER Install
    Install from WheelDir only; do not download.

.EXAMPLE
    # Download on hotspot, then switch back to corporate network and install:
    .\scripts\install-deps.ps1 -Download -WheelDir C:\qnob_wheels
    .\scripts\install-deps.ps1 -Install  -WheelDir C:\qnob_wheels

.EXAMPLE
    # One-shot (unrestricted network):
    .\scripts\install-deps.ps1 -WheelDir C:\qnob_wheels
#>

[CmdletBinding()]
param(
    [string] $WheelDir = "C:\qnob_wheels",
    [string] $VenvDir  = "",
    [switch] $Download,
    [switch] $Install
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

# Resolve paths relative to the Companions\Windows folder (one level up from scripts\)
$CompanionRoot = Split-Path $PSScriptRoot -Parent
if (-not $VenvDir) {
    $VenvDir = Join-Path $CompanionRoot ".venv"
}
$PipExe = Join-Path $VenvDir "Scripts\python.exe"

# ── helpers ────────────────────────────────────────────────────────────────────
function Ensure-Venv {
    if (-not (Test-Path $PipExe)) {
        Write-Host "Creating virtual environment at $VenvDir ..." -ForegroundColor Cyan
        python -m venv $VenvDir
        if ($LASTEXITCODE -ne 0) { throw "Failed to create venv. Is Python 3.11+ on PATH?" }
    }
}

function Run-Pip {
    param([string[]]$Args)
    & $PipExe -m pip @Args
    if ($LASTEXITCODE -ne 0) { throw "pip failed with exit code $LASTEXITCODE" }
}

# All packages required (direct + known transitive deps that pip may miss)
$Packages = @(
    "shiboken6",
    "PySide6_Essentials",
    "PySide6_Addons",
    "qasync",
    "zeroconf",
    "bleak",
    "platformdirs",
    "keyring",
    "jaraco.context",
    "jaraco.functools",
    "jaraco.classes",
    "importlib_metadata",
    "pywin32-ctypes",
    "annotated-types",
    "typing-inspection"
)

# ── Download ────────────────────────────────────────────────────────────────────
function Do-Download {
    Write-Host "`nDownloading wheels to $WheelDir ..." -ForegroundColor Cyan
    New-Item -ItemType Directory -Path $WheelDir -Force | Out-Null

    Ensure-Venv

    # Disable NO_PROXY so the direct connection is used
    $env:NO_PROXY = "*"

    Run-Pip @("download") + $Packages + @("-d", $WheelDir)
    Write-Host "`nAll wheels saved to $WheelDir" -ForegroundColor Green
}

# ── Install ─────────────────────────────────────────────────────────────────────
function Do-Install {
    if (-not (Test-Path $WheelDir)) {
        throw "Wheel directory '$WheelDir' not found. Run with -Download first."
    }

    Write-Host "`nInstalling from $WheelDir ..." -ForegroundColor Cyan
    Ensure-Venv

    Run-Pip @("install", "--no-index", "--find-links", $WheelDir) + $Packages

    # Install the package itself (editable, no extra network calls)
    Push-Location $CompanionRoot
    try {
        Run-Pip @("install", "-e", ".[dev]", "--no-index", "--find-links", $WheelDir, "--no-deps")
    } catch {
        # Fallback without [dev] extras if test tools aren't in WheelDir
        Run-Pip @("install", "-e", ".", "--no-index", "--find-links", $WheelDir, "--no-deps")
    } finally {
        Pop-Location
    }

    Write-Host "`nInstallation complete. Run the companion with:" -ForegroundColor Green
    Write-Host "  `$env:PYTHONPATH = 'src'" -ForegroundColor White
    Write-Host "  $PipExe -m qnob_companion.app.main" -ForegroundColor White
}

# ── Entry point ─────────────────────────────────────────────────────────────────
if ($Download -and $Install) {
    Do-Download
    Do-Install
} elseif ($Download) {
    Do-Download
} elseif ($Install) {
    Do-Install
} else {
    # Neither flag → do both
    Do-Download
    Do-Install
}
