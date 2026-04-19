<#
.SYNOPSIS
    Checks or fixes C++ formatting against the repo's .clang-format.

.DESCRIPTION
    Enumerates project source files (*.cpp, *.hpp, *.h) under the source
    directories, skipping third-party vendored code and build artifacts.
    By default runs clang-format in dry-run mode and fails on any diff.
    With -Fix, rewrites files in place.

.PARAMETER Fix
    Reformat files in place instead of just checking.

.PARAMETER ClangFormat
    Path to clang-format. Auto-detected on PATH or in common install locations.

.EXAMPLE
    PS> .\scripts\check-style.ps1
    PS> .\scripts\check-style.ps1 -Fix
#>
[CmdletBinding()]
param(
    [switch]$Fix,
    [string]$ClangFormat
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string]$m) Write-Host "==> $m" -ForegroundColor Cyan }
function Write-Ok   { param([string]$m) Write-Host "    $m" -ForegroundColor Green }
function Write-Warn { param([string]$m) Write-Host "    $m" -ForegroundColor Yellow }

$repoRoot = Split-Path -Parent $PSScriptRoot
if (-not (Test-Path (Join-Path $repoRoot '.clang-format'))) {
    throw ".clang-format not found at $repoRoot."
}

# --- Locate clang-format ---
if (-not $ClangFormat) {
    $cmd = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($cmd) { $ClangFormat = $cmd.Source }
}
if (-not $ClangFormat -or -not (Test-Path $ClangFormat)) {
    $candidates = @(
        'C:\msys64\ucrt64\bin\clang-format.exe',
        'C:\Program Files\LLVM\bin\clang-format.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\Llvm\x64\bin\clang-format.exe',
        'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\Llvm\x64\bin\clang-format.exe'
    )
    $ClangFormat = $candidates | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $ClangFormat) {
    throw "clang-format not found. Install LLVM or pass -ClangFormat <path>."
}
Write-Step "Using $ClangFormat"

# --- Enumerate source files ---
$sourceDirs = @('control', 'event_engine', 'infrastructure', 'scene_graph', 'rendering_engine', 'rhi', 'external')
$files = foreach ($d in $sourceDirs) {
    $path = Join-Path $repoRoot $d
    if (Test-Path $path) {
        Get-ChildItem -Path $path -Recurse -File -Include '*.cpp','*.hpp','*.h'
    }
}
if (-not $files) { throw "No source files found." }
Write-Ok "$($files.Count) file(s) under $($sourceDirs -join ', ')"

# --- Run clang-format ---
if ($Fix) {
    Write-Step "Fixing in place"
    & $ClangFormat -i --style=file -- $files.FullName
    if ($LASTEXITCODE -ne 0) { throw "clang-format failed (exit $LASTEXITCODE)." }
    Write-Ok "All files reformatted"
    exit 0
}

Write-Step "Checking (dry-run)"
$bad = @()
# clang-format writes diagnostics to stderr; merging with stdout and lowering
# the error preference keeps PowerShell from turning that into a terminating
# error. We rely on $LASTEXITCODE (non-zero == style issue) for pass/fail.
$savedPref = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    foreach ($f in $files) {
        $output = & $ClangFormat --style=file --dry-run --Werror -- $f.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            $bad += [pscustomobject]@{ File = $f.FullName; Output = ($output | Out-String).Trim() }
        }
    }
} finally {
    $ErrorActionPreference = $savedPref
}
if ($bad.Count -eq 0) {
    Write-Ok "No style issues."
    exit 0
}
Write-Host ""
Write-Warn "Style issues in $($bad.Count) file(s):"
foreach ($b in $bad) {
    $rel = $b.File.Substring($repoRoot.Length + 1)
    Write-Host "  - $rel" -ForegroundColor Yellow
}
Write-Host ""
Write-Host "Run .\scripts\check-style.ps1 -Fix to apply formatting." -ForegroundColor Yellow
exit 1
