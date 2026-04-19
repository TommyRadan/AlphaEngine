<#
.SYNOPSIS
    Checks or fixes C++ identifier naming against .clang-tidy (snake_case).

.DESCRIPTION
    Runs clang-tidy's readability-identifier-naming check across the project
    source. A separate build dir (build-tidy/) is configured with Ninja so that
    compile_commands.json is available (the Visual Studio generator doesn't
    produce one). By default reports violations; with -Fix, rewrites in place.

    Vendored code under Vendor/ and generated files under build*/Binaries/ are
    not touched.

.PARAMETER Fix
    Apply rename fixes in place.

.PARAMETER ClangTidy
    Path to clang-tidy. Auto-detected on PATH or in common locations.

.PARAMETER Msys2Root
    MSYS2 root, used for toolchain (Ninja + g++) when configuring the tidy
    build dir. Defaults to C:\msys64.

.EXAMPLE
    PS> .\scripts\check-naming.ps1
    PS> .\scripts\check-naming.ps1 -Fix
#>
[CmdletBinding()]
param(
    [switch]$Fix,
    [string]$ClangTidy,
    [string]$Msys2Root = 'C:\msys64'
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok   { param([string]$m) Write-Host "    $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "    $m" -ForegroundColor Yellow }

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $repoRoot '.clang-tidy'))) {
    throw ".clang-tidy not found at $repoRoot."
}

# --- Locate clang-tidy ---
if (-not $ClangTidy) {
    $cmd = Get-Command clang-tidy -ErrorAction SilentlyContinue
    if ($cmd) { $ClangTidy = $cmd.Source }
}
if (-not $ClangTidy -or -not (Test-Path $ClangTidy)) {
    $candidates = @(
        (Join-Path $Msys2Root 'ucrt64\bin\clang-tidy.exe'),
        'C:\Program Files\LLVM\bin\clang-tidy.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-tidy.exe'
    )
    $ClangTidy = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $ClangTidy) {
    throw "clang-tidy not found. Install LLVM or pass -ClangTidy <path>."
}
Write-Step "Using $ClangTidy"

# --- Configure compile_commands.json via Ninja ---
$tidyBuildDir = Join-Path $repoRoot 'build-tidy'
$compileDb    = Join-Path $tidyBuildDir 'compile_commands.json'
$msys2Bin     = Join-Path $Msys2Root 'ucrt64\bin'
$gpp          = (Join-Path $msys2Bin 'g++.exe')        -replace '\\','/'
$cmakeExe     = (Join-Path $msys2Bin 'cmake.exe')      -replace '\\','/'
$glmInc       = (Join-Path $Msys2Root 'ucrt64\include\glm') -replace '\\','/'
if (-not (Test-Path $cmakeExe)) { $cmakeExe = 'cmake' }

if (-not (Test-Path $compileDb)) {
    Write-Step "Configuring $tidyBuildDir (Ninja) to generate compile_commands.json"
    $env:Path = $msys2Bin + ';' + $env:Path
    $cmakeArgs = @(
        '-S', $repoRoot,
        '-B', $tidyBuildDir,
        '-G', 'Ninja',
        '-DCMAKE_BUILD_TYPE=Debug',
        "-DCMAKE_CXX_COMPILER=$gpp",
        "-DGLM_INCLUDE_DIR=$glmInc",
        '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'
    )
    & $cmakeExe @cmakeArgs | Out-Host
    if ($LASTEXITCODE -ne 0) { throw "CMake configure for tidy db failed ($LASTEXITCODE)." }
}
if (-not (Test-Path $compileDb)) { throw "compile_commands.json was not generated at $compileDb." }
Write-Ok "compile_commands.json: $compileDb"

# --- Enumerate project source files ---
$sourceDirs = @('control', 'event_engine', 'infrastructure', 'scene_graph', 'rendering_engine', 'external', 'asset_manager')
$files = foreach ($d in $sourceDirs) {
    $path = Join-Path $repoRoot $d
    if (Test-Path $path) {
        Get-ChildItem -Path $path -Recurse -File -Include '*.cpp','*.hpp','*.h'
    }
}
if (-not $files) { throw "No source files found." }
Write-Ok "$($files.Count) file(s) under $($sourceDirs -join ', ')"

# --- Run clang-tidy ---
$tidyArgs = @('-p', $tidyBuildDir, '--quiet')
if ($Fix) { $tidyArgs += '--fix' }

# Lower $ErrorActionPreference so clang-tidy stderr doesn't terminate the script.
$savedPref = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
$exitCode = 0
try {
    if ($Fix) {
        Write-Step "Applying identifier renames"
    } else {
        Write-Step "Checking identifier naming"
    }
    # Pass all files in one invocation so cross-file renames stay consistent.
    # clang-tidy: source files come BEFORE any `--`; we don't need extra compiler args.
    & $ClangTidy @tidyArgs $files.FullName 2>&1 | Tee-Object -Variable tidyOutput | Out-Host
    $exitCode = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $savedPref
}

if ($Fix) {
    if ($exitCode -ne 0) { Write-Warn "clang-tidy reported issues during fix (exit $exitCode)." }
    else                 { Write-Ok "Renames applied." }
    exit $exitCode
}

if ($exitCode -eq 0) {
    $warnings = ($tidyOutput | Select-String -Pattern 'warning: invalid case' -SimpleMatch -ErrorAction SilentlyContinue)
    if (-not $warnings -or $warnings.Count -eq 0) {
        Write-Ok "No naming violations."
        exit 0
    }
}
Write-Host ""
Write-Host "Run .\scripts\check-naming.ps1 -Fix to apply renames." -ForegroundColor Yellow
if ($exitCode -eq 0) { exit 1 } else { exit $exitCode }
