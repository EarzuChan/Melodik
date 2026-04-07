param(
    [ValidateSet("debug-dev", "release-prod")]
    [string]$Preset = "debug-dev"
)

. "$PSScriptRoot\common.ps1"

$ctest = Get-CTestExe
& $ctest --preset $Preset --output-on-failure
exit $LASTEXITCODE
