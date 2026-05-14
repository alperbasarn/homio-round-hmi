<#
.SYNOPSIS
    Launch the Qnob Companion app. Creates the venv and installs deps on first run.
#>

$Root = $PSScriptRoot
$Venv = Join-Path $Root ".venv"
$Python = Join-Path $Venv "Scripts\pythonw.exe"
$Pip = Join-Path $Venv "Scripts\pip.exe"

# Create venv if missing
if (-not (Test-Path $Python)) {
    Write-Host "Setting up virtual environment..." -ForegroundColor Cyan
    py -3.12 -m venv $Venv
    if ($LASTEXITCODE -ne 0) { Write-Error "py -3.12 not found. Install Python 3.12 from python.org."; exit 1 }
    & $Pip install -e $Root --quiet
    if ($LASTEXITCODE -ne 0) { Write-Error "pip install failed."; exit 1 }
    Write-Host "Done." -ForegroundColor Green
}

Start-Process $Python -ArgumentList "-m qnob_companion.app.main" -WorkingDirectory $Root
