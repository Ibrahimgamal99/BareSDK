# Quick build script for C++ examples
# Assumes you have echosdk.dll and echosdk.lib already built

param(
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"

Write-Host "=== Building C++ Examples ===" -ForegroundColor Cyan

# Check prerequisites
$dllPath = "..\..\dist\windows\x64\echosdk.dll"
$libPath = "..\..\dist\windows\x64\echosdk.lib"
$headerPath = "..\..\dist\windows\x64\include\echosdk.h"

if (-not (Test-Path $dllPath)) {
    Write-Error "echosdk.dll not found at $dllPath`nPlease build the SDK first or copy the DLL."
    exit 1
}

if (-not (Test-Path $libPath)) {
    Write-Error "echosdk.lib not found at $libPath`nThe import library is required to link against the DLL."
    exit 1
}

if (-not (Test-Path $headerPath)) {
    Write-Error "Header files not found at $headerPath"
    exit 1
}

Write-Host "[OK] Found echosdk.dll" -ForegroundColor Green
Write-Host "[OK] Found echosdk.lib" -ForegroundColor Green
Write-Host "[OK] Found headers" -ForegroundColor Green
Write-Host ""

# Check for CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Error "CMake not found. Please install CMake 3.16 or later."
    exit 1
}

Write-Host "[OK] Found CMake: $(cmake --version | Select-Object -First 1)" -ForegroundColor Green
Write-Host ""

# Configure
Write-Host "Configuring..." -ForegroundColor Yellow
if (Test-Path "build\CMakeCache.txt") {
    Write-Host "Removing old build configuration..." -ForegroundColor Yellow
    Remove-Item -Path "build" -Recurse -Force
}

cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if ($LASTEXITCODE -ne 0) {
    Write-Error "CMake configuration failed"
    exit $LASTEXITCODE
}

Write-Host "[OK] Configuration complete" -ForegroundColor Green
Write-Host ""

# Build
Write-Host "Building $Config..." -ForegroundColor Yellow
cmake --build build --config $Config
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed"
    exit $LASTEXITCODE
}

Write-Host ""
Write-Host "=== Build Complete ===" -ForegroundColor Green
Write-Host ""
Write-Host "Executable: build\$Config\quickstart.exe" -ForegroundColor Cyan
Write-Host ""
Write-Host "To run:" -ForegroundColor Yellow
Write-Host ('  .\build\' + $Config + '\quickstart.exe examples\account.json') -ForegroundColor White
Write-Host ""
