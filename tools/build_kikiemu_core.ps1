# SPDX-License-Identifier: GPL-2.0-or-later
param([string]$OutputDirectory = 'build/kikiemu-core', [switch]$RunUnitTests)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$compiler = (Get-Command clang++ -ErrorAction Stop).Source
if ((& $compiler -dumpmachine).Trim() -ne 'aarch64-w64-windows-gnu') {
    throw 'Use the native MSYS2 CLANGARM64 compiler, not x86_64 or MSYS clang.'
}
$prefix = Split-Path (Split-Path $compiler -Parent) -Parent
if (-not (Test-Path -LiteralPath (Join-Path $prefix 'include/nlohmann/json.hpp'))) {
    throw 'Install mingw-w64-clang-aarch64-nlohmann-json in MSYS2 CLANGARM64.'
}
if (-not [IO.Path]::IsPathRooted($OutputDirectory)) { $OutputDirectory = Join-Path $repoRoot $OutputDirectory }
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$core = @('config', 'runtime', 'process', 'boot', 'disk') | ForEach-Object { Join-Path $repoRoot "src/kikiemu/$_.cpp" }
$flags = @('-std=c++20', '-O2', '-Wall', '-Wextra', '-static', '-municode')
$libs = @('-lbcrypt', '-lole32', '-lshell32', '-luuid')
$inspector = Join-Path $OutputDirectory 'kikiemu-runtime-inspect.exe'
& $compiler @flags @core (Join-Path $repoRoot 'src/kikiemu/runtime_inspect.cpp') -o $inspector @libs
if ($LASTEXITCODE -ne 0) { throw 'Native runtime-inspector build failed.' }
$tests = Join-Path $OutputDirectory 'kikiemu-core-tests.exe'
& $compiler @flags @core (Join-Path $repoRoot 'tests/core_tests.cpp') -o $tests @libs
if ($LASTEXITCODE -ne 0) { throw 'Native core-unit-test build failed.' }
$diskPrototype = Join-Path $OutputDirectory 'kikiemu-disk-prototype.exe'
& $compiler @flags @core (Join-Path $repoRoot 'src/kikiemu/disk_prototype.cpp') -o $diskPrototype @libs
if ($LASTEXITCODE -ne 0) { throw 'Native system-disk-prototype build failed.' }
Write-Output "Built native ARM64 development tools: $OutputDirectory"
Write-Output 'This does not build/install setup.exe, register an instance, change PATH or start QEMU.'
if ($RunUnitTests) {
    & $tests
    if ($LASTEXITCODE -ne 0) { throw 'Core unit tests failed.' }
}
