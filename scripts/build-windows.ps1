# Build libbare for Windows x64.
# Output: dist\windows\x64\bare.lib  +  dist\windows\x64\include\
#
# Prerequisites:
#   - Visual Studio 2022 (or 2019)
#   - vcpkg with x64-windows-static-md triplet + openssl installed:
#       vcpkg install openssl:x64-windows-static-md
#   - cmake (in PATH)

param(
    [string]$BuildType = "Release",
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root = Split-Path -Parent $ScriptDir
$BuildDir = Join-Path $Root "build\windows-x64"

if (-not $VcpkgRoot) {
    Write-Error "VCPKG_ROOT is not set. Install vcpkg and set the environment variable."
    exit 1
}

$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) {
    Write-Error "vcpkg toolchain not found at $Toolchain"
    exit 1
}

Write-Host "=== Configuring Windows x64 ==="
cmake -S $Root -B $BuildDir `
    -G "Visual Studio 17 2022" -A x64 `
    "-DCMAKE_TOOLCHAIN_FILE=$Toolchain" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
    -DLIBBARE_TLS=openssl `
    -DLIBBARE_MODULES_PROFILE=desktop

Write-Host "=== Building ==="
cmake --build $BuildDir --config $BuildType --target libbare

Write-Host "=== Installing ==="
cmake --install $BuildDir --config $BuildType

$Output = Join-Path $Root "dist\windows\x64\bare.lib"
if (Test-Path $Output) {
    $size = (Get-Item $Output).Length / 1MB
    Write-Host ""
    Write-Host "Done. Output: $Output ($([math]::Round($size,1)) MB)"
} else {
    Write-Host "Build complete. Check dist\windows\x64\ for output."
}
