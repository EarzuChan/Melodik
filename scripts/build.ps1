param(
    [ValidateSet("debug-dev", "release-prod")]
    [string]$Preset = "debug-dev"
)

. "$PSScriptRoot\common.ps1"

$cmake = Get-CMakeExe
Invoke-InVsDevShell -CommandLine "`"$cmake`" --preset $Preset"
Invoke-InVsDevShell -CommandLine "`"$cmake`" --build --preset $Preset"
