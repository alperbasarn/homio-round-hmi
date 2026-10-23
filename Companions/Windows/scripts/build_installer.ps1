<#
.SYNOPSIS
    Build the Qnob Companion installer (PyInstaller + optional MSIX packaging).

.DESCRIPTION
    Steps:
      1. Run PyInstaller to produce dist/qnob-companion/ (one-dir).
      2. If Windows SDK makeappx.exe is found AND packaging/Assets/ contains
         the required PNGs, assemble an MSIX package.
      3. Optionally sign the MSIX with signtool.exe (requires -Sign and -CertThumbprint).

.PARAMETER Configuration
    "Release" (default) or "Debug". Debug keeps the PyInstaller console window open.

.PARAMETER Sign
    Sign the MSIX with signtool.exe after packaging.

.PARAMETER CertThumbprint
    SHA-1 thumbprint of the code-signing certificate in the local certificate store.
    Required when -Sign is specified.

.PARAMETER SkipMsix
    Skip MSIX packaging even if Windows SDK is present.

.EXAMPLE
    .\scripts\build_installer.ps1
    .\scripts\build_installer.ps1 -Sign -CertThumbprint ABCDEF1234...
#>
[CmdletBinding()]
param(
    [ValidateSet("Release", "Debug")]
    [string]$Configuration = "Release",

    [switch]$Sign,
    [string]$CertThumbprint = "",
    [switch]$SkipMsix
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$Root = Split-Path -Parent $PSScriptRoot   # Companions/Windows/
Set-Location $Root

# ---------------------------------------------------------------------------
# 0. Read version from package __init__.py
# ---------------------------------------------------------------------------
$VersionLine = Select-String -Path "src\qnob_companion\__init__.py" -Pattern '__version__\s*=\s*"([^"]+)"'
if (-not $VersionLine) { throw "Cannot read __version__ from src\qnob_companion\__init__.py" }
$Version = $VersionLine.Matches[0].Groups[1].Value
Write-Host "Building Qnob Companion $Version ($Configuration)" -ForegroundColor Cyan

# ---------------------------------------------------------------------------
# 1. PyInstaller
# ---------------------------------------------------------------------------
Write-Host "`n[1/3] Running PyInstaller..." -ForegroundColor Cyan

$SpecArgs = @("qnob_companion.spec", "--clean", "--noconfirm")
if ($Configuration -eq "Debug") {
    # Patch spec on-the-fly: rewrite console=True for debug builds. PyInstaller
    # doesn't support per-build overrides, so we inject the env var and hook it
    # in the spec if needed. For now just note it here — edit the spec manually.
    Write-Warning "Debug builds: edit qnob_companion.spec and set console=True, then rebuild."
}

& pyinstaller @SpecArgs
if ($LASTEXITCODE -ne 0) { throw "PyInstaller failed (exit $LASTEXITCODE)" }

$DistDir = Join-Path $Root "dist\qnob-companion"
if (-not (Test-Path $DistDir)) { throw "PyInstaller output not found at $DistDir" }
Write-Host "  -> $DistDir" -ForegroundColor Green

# ---------------------------------------------------------------------------
# 2. MSIX packaging (optional)
# ---------------------------------------------------------------------------
if ($SkipMsix) {
    Write-Host "`n[2/3] Skipping MSIX packaging (-SkipMsix)." -ForegroundColor Yellow
} else {
    Write-Host "`n[2/3] Checking MSIX prerequisites..." -ForegroundColor Cyan

    # Locate makeappx.exe from Windows SDK (10.0.19041+)
    $SdkBin = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.19041.0\x64",
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.22621.0\x64"
    ) | Where-Object { Test-Path (Join-Path $_ "makeappx.exe") } | Select-Object -First 1

    $AssetsDir = Join-Path $Root "packaging\Assets"
    $RequiredAssets = @(
        "Square44x44Logo.png",
        "Square150x150Logo.png",
        "Wide310x150Logo.png",
        "StoreLogo.png"
    )
    $MissingAssets = $RequiredAssets | Where-Object { -not (Test-Path (Join-Path $AssetsDir $_)) }

    if (-not $SdkBin) {
        Write-Warning "makeappx.exe not found — install Windows SDK 10.0.19041+. Skipping MSIX."
    } elseif ($MissingAssets) {
        Write-Warning "Missing MSIX assets in packaging/Assets/: $($MissingAssets -join ', '). Skipping MSIX."
        Write-Warning "Add PNG assets (see packaging/AppxManifest.xml for required sizes)."
    } else {
        Write-Host "  Building MSIX package..." -ForegroundColor Cyan

        # Assemble staging directory
        $StagingDir = Join-Path $Root "dist\_msix_staging"
        if (Test-Path $StagingDir) { Remove-Item -Recurse -Force $StagingDir }
        New-Item -ItemType Directory -Force $StagingDir | Out-Null

        # Copy PyInstaller output as the app payload
        Copy-Item -Recurse $DistDir (Join-Path $StagingDir "qnob-companion")

        # Copy manifest and assets
        Copy-Item (Join-Path $Root "packaging\AppxManifest.xml") $StagingDir
        $AssetsTarget = Join-Path $StagingDir "Assets"
        New-Item -ItemType Directory -Force $AssetsTarget | Out-Null
        Copy-Item (Join-Path $AssetsDir "*") $AssetsTarget

        # Patch version in manifest
        $ManifestPath = Join-Path $StagingDir "AppxManifest.xml"
        $ManifestContent = Get-Content $ManifestPath -Raw
        $FourPartVersion = ($Version -split "\." | Select-Object -First 3) -join "."
        $FourPartVersion = "$FourPartVersion.0"
        $ManifestContent = $ManifestContent -replace 'Version="[^"]+"', "Version=`"$FourPartVersion`""
        Set-Content $ManifestPath $ManifestContent -Encoding UTF8

        # Build MSIX
        $MsixPath = Join-Path $Root "dist\qnob-companion-$Version.msix"
        $MakeAppx = Join-Path $SdkBin "makeappx.exe"
        & $MakeAppx pack /d $StagingDir /p $MsixPath /nv /o
        if ($LASTEXITCODE -ne 0) { throw "makeappx failed (exit $LASTEXITCODE)" }
        Write-Host "  -> $MsixPath" -ForegroundColor Green

        Remove-Item -Recurse -Force $StagingDir

        # ---------------------------------------------------------------------------
        # 3. Code signing (optional)
        # ---------------------------------------------------------------------------
        if ($Sign) {
            Write-Host "`n[3/3] Signing MSIX..." -ForegroundColor Cyan
            if (-not $CertThumbprint) { throw "-CertThumbprint is required when -Sign is specified" }

            $SignTool = $SdkBin.Replace("makeappx.exe", "") | Join-Path -ChildPath "signtool.exe"
            if (-not (Test-Path $SignTool)) { throw "signtool.exe not found at $SignTool" }

            & $SignTool sign /sha1 $CertThumbprint /fd SHA256 /tr http://timestamp.digicert.com /td SHA256 $MsixPath
            if ($LASTEXITCODE -ne 0) { throw "signtool failed (exit $LASTEXITCODE)" }
            Write-Host "  Signed: $MsixPath" -ForegroundColor Green
        } else {
            Write-Host "`n[3/3] Skipping code signing (no -Sign flag)." -ForegroundColor Yellow
            Write-Warning "Unsigned MSIX can only be installed if sideloading is enabled (developer mode or trusted cert)."
        }
    }
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
Write-Host "`nBuild complete." -ForegroundColor Green
Write-Host "  One-dir EXE : $DistDir\qnob-companion.exe"
$MsixCandidate = Join-Path $Root "dist\qnob-companion-$Version.msix"
if (Test-Path $MsixCandidate) {
    Write-Host "  MSIX package: $MsixCandidate"
}
