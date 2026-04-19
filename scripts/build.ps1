<#
.SYNOPSIS
    Configures and builds AlphaEngine on Windows.

.DESCRIPTION
    Detects an installed toolchain and builds the project.
    - If vcpkg is present at -VcpkgRoot, uses the Visual Studio 2022 generator
      with the vcpkg toolchain file (the setup that scripts\setup-windows.ps1
      produces).
    - Otherwise, falls back to MSYS2 UCRT64 (Ninja + g++) if installed.

    Can be run from any directory; resolves the repo root from the script path.

.PARAMETER Toolchain
    auto (default), msvc, or msys2.

.PARAMETER Configuration
    CMake build configuration. Debug, Release, RelWithDebInfo, MinSizeRel.
    Defaults to Release.

.PARAMETER Clean
    Delete the build directory before configuring.

.PARAMETER VcpkgRoot
    vcpkg install location. Defaults to C:\vcpkg.

.PARAMETER Msys2Root
    MSYS2 install location. Defaults to C:\msys64.

.PARAMETER BuildDir
    Override the build directory (default: <repo>/build).

.EXAMPLE
    PS> .\scripts\build.ps1
    PS> .\scripts\build.ps1 -Configuration Debug -Clean
    PS> .\scripts\build.ps1 -Toolchain msys2
#>
[CmdletBinding()]
param(
    [ValidateSet('auto','msvc','msys2')]
    [string]$Toolchain = 'auto',
    [ValidateSet('Debug','Release','RelWithDebInfo','MinSizeRel')]
    [string]$Configuration = 'Release',
    [switch]$Clean,
    [string]$VcpkgRoot = 'C:\vcpkg',
    [string]$Msys2Root = 'C:\msys64',
    [string]$BuildDir
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok   { param([string]$m) Write-Host "    $m" -ForegroundColor Green }
function Fail       { param([string]$m) Write-Host "    $m" -ForegroundColor Red; throw $m }

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $repoRoot 'CMakeLists.txt'))) {
    Fail "CMakeLists.txt not found at $repoRoot. Is this script in the repo's scripts/ folder?"
}
if (-not $BuildDir) { $BuildDir = Join-Path $repoRoot 'build' }

Write-Step "Repo root: $repoRoot"
Write-Ok   "Build dir: $BuildDir"
Write-Ok   "Configuration: $Configuration"

# --- Toolchain detection ---
$vcpkgExe = Join-Path $VcpkgRoot 'vcpkg.exe'
$msys2Gpp = Join-Path $Msys2Root 'ucrt64\bin\g++.exe'

if ($Toolchain -eq 'auto') {
    if (Test-Path $vcpkgExe)      { $Toolchain = 'msvc' }
    elseif (Test-Path $msys2Gpp)  { $Toolchain = 'msys2' }
    else { Fail "No toolchain detected. Run .\scripts\setup-windows.ps1 first (or install MSYS2 at $Msys2Root)." }
}
Write-Step "Toolchain: $Toolchain"

if ($Toolchain -eq 'msvc') {
    if (-not (Test-Path $vcpkgExe)) { Fail "vcpkg not found at $vcpkgExe." }
} else {
    if (-not (Test-Path $msys2Gpp)) { Fail "MSYS2 g++ not found at $msys2Gpp." }
}

# --- Optional clean ---
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Step "Cleaning $BuildDir"
    Remove-Item -Recurse -Force -LiteralPath $BuildDir
    Write-Ok "Removed"
}

# --- Find cmake ---
$cmakeCmd = Get-Command cmake -ErrorAction SilentlyContinue
$cmake = if ($cmakeCmd) { $cmakeCmd.Source } else { $null }
if (-not $cmake) {
    $fallback = if ($Toolchain -eq 'msys2') { Join-Path $Msys2Root 'ucrt64\bin\cmake.exe' } else { $null }
    if ($fallback -and (Test-Path $fallback)) { $cmake = $fallback }
    else { Fail "cmake not found on PATH. Install it (setup-windows.ps1 handles this) or point PATH at one." }
}
Write-Ok "CMake: $cmake"

# --- Configure + build ---
$configArgs = @('-S', $repoRoot, '-B', $BuildDir)
$buildArgs  = @('--build', $BuildDir)

if ($Toolchain -eq 'msvc') {
    $vcpkgFwd = $VcpkgRoot -replace '\\','/'
    $configArgs += @(
        '-G', 'Visual Studio 17 2022',
        '-A', 'x64',
        "-DCMAKE_TOOLCHAIN_FILE=$vcpkgFwd/scripts/buildsystems/vcpkg.cmake"
    )
    $buildArgs += @('--config', $Configuration)
    $expectedExe = Join-Path $repoRoot "Binaries\$Configuration\AlphaEngine.exe"
} else {
    $gppFwd = ($msys2Gpp -replace '\\','/')
    $env:Path = (Join-Path $Msys2Root 'ucrt64\bin') + ';' + $env:Path
    $configArgs += @(
        '-G', 'Ninja',
        "-DCMAKE_BUILD_TYPE=$Configuration",
        "-DCMAKE_CXX_COMPILER=$gppFwd"
    )
    $expectedExe = Join-Path $repoRoot "Binaries\AlphaEngine.exe"
}

Write-Step "Configuring"
& $cmake @configArgs
if ($LASTEXITCODE -ne 0) { Fail "cmake configure failed (exit $LASTEXITCODE)." }
Write-Ok "Configure succeeded"

Write-Step "Building"
& $cmake @buildArgs
if ($LASTEXITCODE -ne 0) { Fail "cmake build failed (exit $LASTEXITCODE)." }
Write-Ok "Build succeeded"

# --- Verify output ---
Write-Step "Verifying output"
if (-not (Test-Path $expectedExe)) { Fail "Expected binary missing: $expectedExe" }
$size = (Get-Item $expectedExe).Length
Write-Ok ("Built: {0} ({1:N0} bytes)" -f $expectedExe, $size)

Write-Host ""
Write-Host "Build complete." -ForegroundColor Green
