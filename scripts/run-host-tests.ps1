# BLD-09: run the model layer's host tests from Windows by delegating to WSL, where g++ lives.
[CmdletBinding()]
param(
    [string]$WslDistro = "Ubuntu-22.04"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$fullPath = [System.IO.Path]::GetFullPath($scriptRoot) -replace "\\", "/"
if ($fullPath -notmatch "^([A-Za-z]):/(.*)$") {
    throw "Unsupported path for WSL conversion: $scriptRoot"
}
$wslScripts = "/mnt/$($matches[1].ToLowerInvariant())/$($matches[2])"

& wsl.exe -d $WslDistro -e bash -lc "bash '$wslScripts/run-host-tests.sh'"
if ($LASTEXITCODE -ne 0) {
    throw "Host tests failed (exit $LASTEXITCODE)"
}
