# Build the SDK (if needed) and install the Python package on Windows.
# Run from the repo root or from bindings/python/.
#
# Prerequisites: Visual Studio 2022, vcpkg (VCPKG_ROOT set), Python >=3.9
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$Root      = Split-Path -Parent (Split-Path -Parent $ScriptDir)
$DistDir   = Join-Path $Root "dist\windows\x64"
$DllSrc    = Join-Path $DistDir "baresdk.dll"
$DllDst    = Join-Path $ScriptDir "baresdk\baresdk.dll"

# ── 1. Build the SDK if the DLL is missing ───────────────────────────────────
if (-not (Test-Path $DllSrc)) {
    Write-Host "==> DLL not found — building SDK first..."
    & "$Root\scripts\build-windows.ps1" -VcpkgRoot $VcpkgRoot
}

# ── 2. Copy DLL into the package directory ───────────────────────────────────
Write-Host "==> Copying baresdk.dll into package..."
Copy-Item $DllSrc $DllDst -Force

# ── 3. Install the Python package ────────────────────────────────────────────
Write-Host "==> Installing Python package..."
pip install -e $ScriptDir

Write-Host ""
Write-Host "Done. Run an example:"
Write-Host "  python $ScriptDir\examples\quickstart.py account.json"
