[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("esp32s3_lcd128", "esp32s3_amoled175", "esp32c6_amoled143")]
    [string]$Variant,

    [string]$WslDistro = "Ubuntu-22.04",
    [string]$EspIdfExport = "~/esp/esp-idf/export.sh"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Convert-ToWslPath {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Path
    )

    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $normalized = $fullPath -replace "\\", "/"
    if ($normalized -match "^([A-Za-z]):/(.*)$") {
        return "/mnt/$($matches[1].ToLowerInvariant())/$($matches[2])"
    }

    throw "Unsupported path for WSL conversion: $Path"
}

function Quote-BashValue {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Value
    )

    $doubleQuote = [string][char]34
    $escapedSingleQuote = "'" + $doubleQuote + "'" + $doubleQuote + "'"
    return "'" + ($Value -replace "'", $escapedSingleQuote) + "'"
}

$repoRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Join-Path $repoRoot "esp-idf"
$outputRoot = Join-Path $repoRoot "Hardware Ref"
$publishRoot = Join-Path $repoRoot "Binaries"
$otaRoot = Join-Path $repoRoot "OTA"
$appName = "qnob-screen"
$lockFile = Join-Path $projectDir "dependencies.lock"
$lockBackup = $null
$lockFilePresent = Test-Path $lockFile

$variantMap = @{
    "esp32s3_lcd128" = @{
        DisplayName       = "ESP32-S3 LCD 1.28"
        IdfTarget         = "esp32s3"
        OtaNamePrefix     = "S3-128"
        Defaults          = @("sdkconfig.defaults", "sdkconfig.defaults.esp32s3", "sdkconfig.defaults.esp32s3_lcd128")
        BuildDir          = Join-Path $outputRoot "build_s3_lcd128"
        GeneratedSdkconfig = Join-Path $outputRoot "sdkconfig.esp32s3_lcd128"
    }
    "esp32s3_amoled175" = @{
        DisplayName       = "ESP32-S3 AMOLED 1.75"
        IdfTarget         = "esp32s3"
        OtaNamePrefix     = "S3-175"
        Defaults          = @("sdkconfig.defaults", "sdkconfig.defaults.esp32s3", "sdkconfig.defaults.esp32s3_amoled175")
        BuildDir          = Join-Path $outputRoot "build_s3_amoled175"
        GeneratedSdkconfig = Join-Path $outputRoot "sdkconfig.esp32s3_amoled175"
    }
    "esp32c6_amoled143" = @{
        DisplayName       = "ESP32-C6 AMOLED 1.43"
        IdfTarget         = "esp32c6"
        OtaNamePrefix     = "C6-143"
        Defaults          = @("sdkconfig.defaults", "sdkconfig.defaults.esp32c6", "sdkconfig.defaults.esp32c6_amoled143")
        BuildDir          = Join-Path $outputRoot "build_c6_amoled143"
        GeneratedSdkconfig = Join-Path $outputRoot "sdkconfig.esp32c6_amoled143"
    }
}

$config = $variantMap[$Variant]
if ($null -eq $config) {
    throw "Unknown variant: $Variant"
}

$projectDirWsl = Convert-ToWslPath -Path $projectDir
$buildDirWsl = Convert-ToWslPath -Path $config.BuildDir
$sdkconfigWsl = Convert-ToWslPath -Path $config.GeneratedSdkconfig
$defaultsArg = $config.Defaults -join ";"

$bashCommand = @(
    "set -e"
    "source $(Quote-BashValue -Value $EspIdfExport) >/dev/null"
    "cd $(Quote-BashValue -Value $projectDirWsl)"
    "idf.py -B $(Quote-BashValue -Value $buildDirWsl) -DIDF_TARGET=$($config.IdfTarget) -DSDKCONFIG=$(Quote-BashValue -Value $sdkconfigWsl) -DSDKCONFIG_DEFAULTS=$(Quote-BashValue -Value $defaultsArg) build"
) -join " && "

Write-Host "[$Variant] Building $($config.DisplayName)..."
Write-Host "[$Variant] Output dir: $($config.BuildDir)"

if ($lockFilePresent) {
    $lockBackup = Join-Path ([System.IO.Path]::GetTempPath()) ("dependencies.lock.{0}.bak" -f [System.Guid]::NewGuid().ToString("N"))
    Copy-Item -Force -LiteralPath $lockFile -Destination $lockBackup
}

try {
    & wsl.exe -d $WslDistro -e bash -lc $bashCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Build failed for $Variant"
    }

    $artifactDir = Join-Path $publishRoot $Variant
    New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
    New-Item -ItemType Directory -Force -Path $otaRoot | Out-Null

    $artifacts = @(
        @{ Source = Join-Path $config.BuildDir "$appName.bin"; Destination = Join-Path $artifactDir "$appName.bin" },
        @{ Source = Join-Path $config.BuildDir "$appName.elf"; Destination = Join-Path $artifactDir "$appName.elf" },
        @{ Source = Join-Path $config.BuildDir "$appName.map"; Destination = Join-Path $artifactDir "$appName.map" },
        @{ Source = Join-Path $config.BuildDir "bootloader\\bootloader.bin"; Destination = Join-Path $artifactDir "bootloader.bin" },
        @{ Source = Join-Path $config.BuildDir "partition_table\\partition-table.bin"; Destination = Join-Path $artifactDir "partition-table.bin" },
        @{ Source = Join-Path $config.BuildDir "ota_data_initial.bin"; Destination = Join-Path $artifactDir "ota_data_initial.bin" },
        @{ Source = Join-Path $config.BuildDir "flash_args"; Destination = Join-Path $artifactDir "flash_args" },
        @{ Source = Join-Path $config.BuildDir "flasher_args.json"; Destination = Join-Path $artifactDir "flasher_args.json" }
    )

    foreach ($artifact in $artifacts) {
        if (-not (Test-Path $artifact.Source)) {
            throw "Expected artifact not found: $($artifact.Source)"
        }

        Copy-Item -Force -LiteralPath $artifact.Source -Destination $artifact.Destination
    }

    $projectDescriptionPath = Join-Path $config.BuildDir "project_description.json"
    if (-not (Test-Path $projectDescriptionPath)) {
        throw "Expected artifact not found: $projectDescriptionPath"
    }

    $projectDescription = Get-Content -LiteralPath $projectDescriptionPath -Raw | ConvertFrom-Json
    $projectVersion = [string]$projectDescription.project_version
    if ([string]::IsNullOrWhiteSpace($projectVersion)) {
        $projectVersion = "unknown"
    }

    $safeVersion = [regex]::Replace($projectVersion, "[^A-Za-z0-9._-]", "_")
    $otaArtifactPath = Join-Path $otaRoot ("{0}-V{1}.bin" -f $config.OtaNamePrefix, $safeVersion)
    Copy-Item -Force -LiteralPath (Join-Path $config.BuildDir "$appName.bin") -Destination $otaArtifactPath

    Write-Host "[$Variant] Published artifacts: $artifactDir"
    Write-Host "[$Variant] Published OTA artifact: $otaArtifactPath"
}
finally {
    if ($lockBackup -and (Test-Path $lockBackup)) {
        if ($lockFilePresent) {
            Copy-Item -Force -LiteralPath $lockBackup -Destination $lockFile
        } elseif (Test-Path $lockFile) {
            Remove-Item -LiteralPath $lockFile -Force
        }
        Remove-Item -LiteralPath $lockBackup -Force
    }
}
