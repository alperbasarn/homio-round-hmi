param(
    [string]$Port = ''
)

$ErrorActionPreference = 'Stop'
$py = 'C:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'
$idf = 'C:\Espressif\frameworks\esp-idf-v5.5.1'
$buildDir = 'C:\repos\p\qnob\homio-round-hmi\build_s3_amoled175'
$sdkconfigOut = 'C:\repos\p\qnob\homio-round-hmi\sdkconfig.esp32s3_amoled175'

$env:IDF_PATH = $idf
$env:IDF_TOOLS_PATH = 'C:\Espressif'

$exports = & $py "$idf\tools\idf_tools.py" export --format key-value
foreach ($line in $exports) {
    $pair = $line -split '=', 2
    if ($pair.Count -eq 2) {
        Set-Item -Path ("Env:" + $pair[0]) -Value $pair[1]
    }
}

Set-Location 'C:\repos\p\qnob\homio-round-hmi\esp-idf'

$idfArgs = @(
    "$idf\tools\idf.py",
    '-B', $buildDir,
    '-DIDF_TARGET=esp32s3',
    "-DSDKCONFIG=$sdkconfigOut",
    '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_amoled175'
)

if ($Port -ne '') {
    $idfArgs += '-p'
    $idfArgs += $Port
}

$idfArgs += 'build'
& $py @idfArgs
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$flashArgs = @(
    "$idf\tools\idf.py",
    '-B', $buildDir,
    '-DIDF_TARGET=esp32s3',
    "-DSDKCONFIG=$sdkconfigOut",
    '-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.esp32s3;sdkconfig.defaults.esp32s3_amoled175'
)
if ($Port -ne '') {
    $flashArgs += '-p'
    $flashArgs += $Port
}
$flashArgs += 'flash'
& $py @flashArgs
