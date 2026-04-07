param(
    [ValidateSet("release-prod")]
    [string]$Preset = "release-prod"
)

. "$PSScriptRoot\common.ps1"

$cmake = Get-CMakeExe
Invoke-InVsDevShell -CommandLine "`"$cmake`" --preset $Preset"
Invoke-InVsDevShell -CommandLine "`"$cmake`" --build --preset $Preset"

$buildDir = Join-Path $PSScriptRoot "..\\build\\$Preset"
$outputDir = Join-Path $buildDir "dist"
New-Item -ItemType Directory -Force -Path $outputDir | Out-Null

$exe = Join-Path $buildDir "melodick_standalone_bootstrap.exe"
if (Test-Path $exe) {
    Copy-Item $exe (Join-Path $outputDir "melodick_standalone_bootstrap.exe") -Force
}

@{
    name = "MeloDick"
    version = "0.2026.04.07-dev"
} | ConvertTo-Json | Set-Content -Encoding UTF8 (Join-Path $outputDir "version.json")

Write-Host "Package output:" $outputDir
