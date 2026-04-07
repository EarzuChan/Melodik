function Get-VsInstallationPath {
    $vswhere = "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "vswhere.exe not found at $vswhere"
    }

    $path = & $vswhere -products * -latest -property installationPath
    if (-not $path) {
        throw "No Visual Studio Build Tools installation found"
    }
    return $path.Trim()
}

function Invoke-InVsDevShell {
    param(
        [Parameter(Mandatory = $true)]
        [string]$CommandLine
    )

    $vsPath = Get-VsInstallationPath
    $vcvars = Join-Path $vsPath "VC\Auxiliary\Build\vcvars64.bat"
    if (-not (Test-Path $vcvars)) {
        throw "vcvars64.bat not found at $vcvars"
    }

    cmd /c "`"$vcvars`" && $CommandLine"
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

function Get-CMakeExe {
    $vsPath = Get-VsInstallationPath
    $cmake = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (-not (Test-Path $cmake)) {
        throw "cmake.exe not found at $cmake"
    }
    return $cmake
}

function Get-CTestExe {
    $vsPath = Get-VsInstallationPath
    $ctest = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe"
    if (-not (Test-Path $ctest)) {
        throw "ctest.exe not found at $ctest"
    }
    return $ctest
}

