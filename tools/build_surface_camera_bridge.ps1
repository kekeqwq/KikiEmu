param(
    [string]$OutputPath,
    [switch]$ProbeRealCameras
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$sourcePath = Join-Path $PSScriptRoot 'surface_camera_bridge.cpp'
$winrtSourcePath = Join-Path $PSScriptRoot 'surface_camera_winrt.cpp'

$compilerCommand = Get-Command clang++ -ErrorAction SilentlyContinue
if (-not $compilerCommand) {
    throw 'clang++ was not found. Add the MSYS2 CLANGARM64 bin directory to PATH.'
}
$compiler = $compilerCommand.Source
$target = (& $compiler -dumpmachine).Trim()
if ($target -ne 'aarch64-w64-windows-gnu') {
    throw "Expected native Windows ARM64 clang++ (aarch64-w64-windows-gnu), found '$target'."
}
$winrtHeaders = Join-Path (Split-Path (Split-Path $compiler -Parent) -Parent) 'include\winrt\Windows.Media.Capture.Frames.h'
if (-not (Test-Path -LiteralPath $winrtHeaders)) {
    throw 'C++/WinRT headers are missing. In MSYS2 CLANGARM64, run: pacman -S --needed mingw-w64-clang-aarch64-cppwinrt'
}

if (-not $OutputPath) {
    $OutputPath = Join-Path $PSScriptRoot 'build\surface-camera-bridge.exe'
}
if (-not [System.IO.Path]::IsPathRooted($OutputPath)) {
    $OutputPath = Join-Path $repoRoot $OutputPath
}
$outputDirectory = Split-Path -Parent $OutputPath
New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

& $compiler -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter `
    $sourcePath $winrtSourcePath -o $OutputPath `
    -lole32 -lmfplat -lmfreadwrite -lmf -lmfuuid -lws2_32 -luuid -lwindowsapp
if ($LASTEXITCODE -ne 0) {
    throw "Surface camera bridge build failed with exit code $LASTEXITCODE."
}

Write-Output "Built native Windows ARM64 camera bridge: $OutputPath"
if ($ProbeRealCameras) {
    & $OutputPath --list
    if ($LASTEXITCODE -ne 0) { throw 'Media Foundation camera enumeration failed.' }
    & $OutputPath --probe rear
    if ($LASTEXITCODE -ne 0) { throw 'Rear camera one-frame probe failed.' }
    & $OutputPath --probe front
    if ($LASTEXITCODE -ne 0) { throw 'Front camera one-frame probe failed.' }
    & $OutputPath --switch-test 3
    if ($LASTEXITCODE -ne 0) { throw 'Sequential front/rear switching test failed.' }
}
