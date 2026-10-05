#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory)][string]$Msys2,
    [Parameter(Mandatory)][string]$KitDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$LibraryDirectory
)
# Relink a source kit with replacement compatible LGPL/static libraries.
# This does NOT run/install any result or operate a registered VM.
$ErrorActionPreference='Stop'
$kit=(Resolve-Path -LiteralPath $KitDirectory).Path
$prefix=Join-Path (Resolve-Path -LiteralPath $Msys2).Path 'clangarm64'
$compiler=Join-Path $prefix 'bin/clang++.exe'
if ((& $compiler -dumpmachine).Trim() -ne 'aarch64-w64-windows-gnu') { throw 'Use native CLANGARM64.' }
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Relinking requires a NEW output directory.' }
[void][IO.Directory]::CreateDirectory($output)
$objects=@('config','runtime','process','child','transport','boot','disk','storage','lifecycle','registry','schema','package','ab','manager','session','installation') |
    ForEach-Object {Join-Path $kit "objects/$_.o"}
$resource=Join-Path $kit 'objects/app-resource.o'
$search=@()
if ($LibraryDirectory) { $search += '-L' + (Resolve-Path -LiteralPath $LibraryDirectory).Path }
$search += '-L' + (Join-Path $kit 'libraries')
$flags=@('-std=c++20','-O2','-static','-municode')
$libraries=@('-larchive','-l:libz.dll.a','-lbz2','-llzma','-lb2','-llz4','-lzstd','-lcrypto','-liconv','-lcharset','-lexpat','-lpcre2-posix','-lpcre2-8','-lcrypt32','-lbcrypt','-lole32','-lshell32','-luuid','-liphlpapi','-lws2_32')
foreach ($entry in @('cli','desktop')) {
    $extra=@(); $name='kikiemu.exe'
    if ($entry -eq 'desktop') { $extra=@('-mwindows'); $name='kikiemu-desktop.exe' }
    & $compiler @flags @extra @search @objects $resource (Join-Path $kit "objects/$entry.o") -o (Join-Path $output $name) @libraries
    if ($LASTEXITCODE -ne 0) { throw "Relinking failed: $name" }
}
& $compiler @flags -mwindows @search (Join-Path $kit 'objects/installation.o') (Join-Path $kit 'objects/setup_helper.o') $resource -o (Join-Path $output 'kikiemu-setup-helper.exe') -lshell32 -lole32 -luuid -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Helper relinking failed.' }
& $compiler -O2 -static @search (Join-Path $kit 'objects/surface_camera_bridge.o') (Join-Path $kit 'objects/surface_camera_winrt.o') -o (Join-Path $output 'surface-camera-bridge.exe') -lole32 -lmfplat -lmfreadwrite -lmf -lmfuuid -lws2_32 -luuid -lwindowsapp -lonecore
if ($LASTEXITCODE -ne 0) { throw 'Camera relinking failed.' }
Copy-Item -LiteralPath (Join-Path $kit 'runtime/zlib1.dll') -Destination $output
Write-Host "Relinked native executables (NOT executed/installed): $output"
