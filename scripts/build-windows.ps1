# Build baresdk for Windows x64.
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
    -DBARESDK_TLS=openssl `
    -DBARESDK_MODULES_PROFILE=desktop

Write-Host "=== Building ==="
cmake --build $BuildDir --config $BuildType --target baresdk

Write-Host "=== Installing ==="
cmake --install $BuildDir --config $BuildType

$StaticLib = Join-Path $Root "dist\windows\x64\bare.lib"
if (Test-Path $StaticLib) {
    $size = (Get-Item $StaticLib).Length / 1MB
    Write-Host ""
    Write-Host "Static lib: $StaticLib ($([math]::Round($size,1)) MB)"
} else {
    Write-Host "Build complete. Check dist\windows\x64\ for output."
}

# ── Link shared library (DLL, zero extra runtime deps) ───────────────────────
# vcpkg x64-windows-static-md builds OpenSSL and zlib as static .lib files,
# so we link them directly — no OpenSSL DLLs needed at runtime.
$VcpkgLibs  = Join-Path $VcpkgRoot "installed\x64-windows-static-md\lib"
$SslLib     = Join-Path $VcpkgLibs "libssl.lib"
$CryptoLib  = Join-Path $VcpkgLibs "libcrypto.lib"
$ZlibLib    = Join-Path $VcpkgLibs "zlib.lib"

$DllPath = Join-Path $Root "dist\windows\x64\baresdk.dll"
Write-Host ""
Write-Host "=== Linking $DllPath ==="
link.exe /DLL /NOLOGO `
    /OUT:$DllPath `
    /WHOLEARCHIVE:$StaticLib `
    $SslLib $CryptoLib $ZlibLib `
    ws2_32.lib iphlpapi.lib crypt32.lib secur32.lib bcrypt.lib

Write-Host ""
Write-Host "Done. Output:"
Get-Item $StaticLib, $DllPath | Select-Object Name, @{N="MB";E={[math]::Round($_.Length/1MB,1)}}
