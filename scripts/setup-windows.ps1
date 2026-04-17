<#
.SYNOPSIS
    Bootstraps a Windows 11 machine so AlphaEngine can be built and run with MSVC.

.DESCRIPTION
    Installs Visual Studio 2022 Build Tools (C++ workload) and vcpkg, then uses
    vcpkg to install SDL2 and GLM. Safe to re-run: every step skips work
    that has already been done.

    After completion, build from the repo root with:

        cmake -S . -B build -G "Visual Studio 17 2022" -A x64 `
            -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
            -DGLM_INCLUDE_DIR=C:/vcpkg/installed/x64-windows/include
        cmake --build build --config Release

    The resulting binary is written to Binaries\Release\AlphaEngine.exe.

.PARAMETER VcpkgRoot
    Location where vcpkg is (or will be) installed. Defaults to C:\vcpkg.

.EXAMPLE
    PS> .\scripts\setup-windows.ps1
#>
[CmdletBinding()]
param(
    [string]$VcpkgRoot = 'C:\vcpkg'
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$Message) Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Ok   { param([string]$Message) Write-Host "    $Message" -ForegroundColor Green }

if (-not [System.Environment]::OSVersion.Platform.ToString().StartsWith('Win')) {
    throw "This script only runs on Windows."
}

Write-Step "Checking for winget"
$winget = Get-Command winget -ErrorAction SilentlyContinue
if (-not $winget) {
    throw "winget is not available. Install 'App Installer' from the Microsoft Store, then re-run this script."
}
Write-Ok "winget found"

Write-Step "Installing Git (if missing)"
if (Get-Command git -ErrorAction SilentlyContinue) {
    Write-Ok "Git already installed"
} else {
    winget install --id Git.Git --silent --accept-package-agreements --accept-source-agreements | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Failed to install Git (exit $LASTEXITCODE)." }
    # Refresh PATH for this session so `git` is usable below.
    $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
    Write-Ok "Git installed"
}

Write-Step "Installing Visual Studio 2022 Build Tools with C++ workload (if missing)"
# vswhere.exe ships with any VS installation and is the canonical way to detect one.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$hasVcTools = $false
if (Test-Path $vswhere) {
    $found = & $vswhere -products '*' -requires Microsoft.VisualStudio.Workload.VCTools -latest -property installationPath 2>$null
    if ($LASTEXITCODE -eq 0 -and $found) { $hasVcTools = $true }
}
if ($hasVcTools) {
    Write-Ok "Visual Studio Build Tools with C++ workload already installed"
} else {
    Write-Host "    Installing - this is a multi-GB download and may take a while..."
    winget install --id Microsoft.VisualStudio.2022.BuildTools `
        --silent --accept-package-agreements --accept-source-agreements `
        --override "--wait --quiet --norestart --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended" | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Failed to install VS Build Tools (exit $LASTEXITCODE)." }
    Write-Ok "Visual Studio Build Tools installed"
}

Write-Step "Installing CMake (if missing)"
if (Get-Command cmake -ErrorAction SilentlyContinue) {
    Write-Ok "CMake already on PATH"
} else {
    winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "Failed to install CMake (exit $LASTEXITCODE)." }
    $env:Path = [Environment]::GetEnvironmentVariable('Path','Machine') + ';' + [Environment]::GetEnvironmentVariable('Path','User')
    Write-Ok "CMake installed"
}

Write-Step "Setting up vcpkg at $VcpkgRoot"
if (-not (Test-Path $VcpkgRoot)) {
    git clone --depth 1 https://github.com/microsoft/vcpkg.git $VcpkgRoot
    if ($LASTEXITCODE -ne 0) { throw "git clone of vcpkg failed." }
}
$vcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
if (-not (Test-Path $vcpkgExe)) {
    & (Join-Path $VcpkgRoot 'bootstrap-vcpkg.bat') -disableMetrics
    if ($LASTEXITCODE -ne 0) { throw "vcpkg bootstrap failed." }
}
Write-Ok "vcpkg ready at $VcpkgRoot"

Write-Step "Installing SDL2, GLM via vcpkg (x64-windows)"
& $vcpkgExe install sdl2:x64-windows glm:x64-windows --recurse | Out-Host
if ($LASTEXITCODE -ne 0) { throw "vcpkg install failed." }
Write-Ok "Libraries installed"

$vcpkgFwd = $VcpkgRoot -replace '\\','/'
$template = @'

Environment is ready.

To build AlphaEngine from the repository root, run:

    cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE={0}/scripts/buildsystems/vcpkg.cmake -DGLM_INCLUDE_DIR={0}/installed/x64-windows/include/glm
    cmake --build build --config Release

The binary will be produced at Binaries\Release\AlphaEngine.exe.
'@
Write-Host ($template -f $vcpkgFwd) -ForegroundColor Green
