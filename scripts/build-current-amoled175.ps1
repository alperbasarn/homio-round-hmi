$ErrorActionPreference = 'Stop'
$py = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$idf = 'C:\Espressif\frameworks\esp-idf-v5.5.1'
$repoRoot = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $repoRoot 'helpers\build_s3_amoled175'
$sdkconfigOut = Join-Path $repoRoot 'helpers\sdkconfig.esp32s3_amoled175'
$artifactDir = Join-Path $repoRoot 'binaries\esp32s3_amoled175'
$otaRoot = Join-Path $repoRoot 'binaries\ota'

$env:IDF_PATH = $idf
$env:IDF_TOOLS_PATH = 'C:\Espressif'

$exports = & $py "$idf\tools\idf_tools.py" export --format key-value
foreach ($line in $exports) {
    $pair = $line -split '=', 2
    if ($pair.Count -eq 2) {
        Set-Item -Path ("Env:" + $pair[0]) -Value $pair[1]
    }
}

Set-Location (Join-Path $repoRoot 'src\esp-idf')

& $py "$idf\tools\idf.py" `
    -B $buildDir `
    -DIDF_TARGET=esp32s3 `
    "-DSDKCONFIG=$sdkconfigOut" `
    "-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_amoled175" `
    build

New-Item -ItemType Directory -Force -Path $artifactDir | Out-Null
New-Item -ItemType Directory -Force -Path $otaRoot | Out-Null

$artifacts = @(
    @{ Source = Join-Path $buildDir 'qnob-screen.bin'; Destination = Join-Path $artifactDir 'qnob-screen.bin' },
    @{ Source = Join-Path $buildDir 'qnob-screen.elf'; Destination = Join-Path $artifactDir 'qnob-screen.elf' },
    @{ Source = Join-Path $buildDir 'qnob-screen.map'; Destination = Join-Path $artifactDir 'qnob-screen.map' },
    @{ Source = Join-Path $buildDir 'bootloader\bootloader.bin'; Destination = Join-Path $artifactDir 'bootloader.bin' },
    @{ Source = Join-Path $buildDir 'partition_table\partition-table.bin'; Destination = Join-Path $artifactDir 'partition-table.bin' },
    @{ Source = Join-Path $buildDir 'ota_data_initial.bin'; Destination = Join-Path $artifactDir 'ota_data_initial.bin' },
    @{ Source = Join-Path $buildDir 'flash_args'; Destination = Join-Path $artifactDir 'flash_args' },
    @{ Source = Join-Path $buildDir 'flasher_args.json'; Destination = Join-Path $artifactDir 'flasher_args.json' }
)

foreach ($artifact in $artifacts) {
    if (-not (Test-Path $artifact.Source)) {
        throw "Expected artifact not found: $($artifact.Source)"
    }

    Copy-Item -Force -LiteralPath $artifact.Source -Destination $artifact.Destination
}

$projectDescriptionPath = Join-Path $buildDir 'project_description.json'
$projectDescription = Get-Content -LiteralPath $projectDescriptionPath -Raw | ConvertFrom-Json
$projectVersion = [string]$projectDescription.project_version
if ([string]::IsNullOrWhiteSpace($projectVersion)) {
    $projectVersion = 'unknown'
}

$safeVersion = [regex]::Replace($projectVersion, '[^A-Za-z0-9._-]', '_')
$otaArtifactPath = Join-Path $otaRoot ("S3-175-V{0}.bin" -f $safeVersion)
Copy-Item -Force -LiteralPath (Join-Path $buildDir 'qnob-screen.bin') -Destination $otaArtifactPath
