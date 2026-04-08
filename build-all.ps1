[CmdletBinding()]
param(
    [ValidateSet("esp32s3_lcd128", "esp32s3_amoled175", "esp32c6_amoled143")]
    [string[]]$Variant = @("esp32s3_lcd128", "esp32s3_amoled175", "esp32c6_amoled143"),

    [string]$WslDistro = "Ubuntu-22.04",
    [string]$EspIdfExport = "~/esp/esp-idf/export.sh"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildVariantScript = Join-Path $scriptRoot "build-variant.ps1"
$results = New-Object System.Collections.Generic.List[object]

foreach ($name in $Variant) {
    Write-Host ""
    Write-Host "=== $name ==="

    try {
        & $buildVariantScript -Variant $name -WslDistro $WslDistro -EspIdfExport $EspIdfExport
        if ($LASTEXITCODE -ne 0) {
            throw "Build script exited with code $LASTEXITCODE"
        }

        $results.Add([pscustomobject]@{
            Variant = $name
            Status  = "OK"
        })
    } catch {
        Write-Error "$name failed: $($_.Exception.Message)"
        $results.Add([pscustomobject]@{
            Variant = $name
            Status  = "FAILED"
        })
    }
}

Write-Host ""
Write-Host "Build summary:"
$results | Format-Table -AutoSize

if ($results.Status -contains "FAILED") {
    exit 1
}
