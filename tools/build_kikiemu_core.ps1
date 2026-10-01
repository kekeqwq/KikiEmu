# SPDX-License-Identifier: GPL-2.0-or-later
param([string]$OutputDirectory = 'build/kikiemu-core', [switch]$RunUnitTests, [string]$ProducerFixture)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'build_receipt.ps1')
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
$receiptPath = Join-Path $OutputDirectory 'core-build-receipt.json'
Initialize-KikiBuildReceipt -Path $receiptPath -Kind 'kikiemu-core'
$sourceInputs = @((Get-ChildItem -LiteralPath (Join-Path $repoRoot 'src/kikiemu') -File -Recurse).FullName)
$sourceInputs += @('tests/core_tests.cpp', 'tests/wire_fixture.hpp', 'tests/system_fixture.hpp', 'tests/system_regression.cpp',
    'tools/build_kikiemu_core.ps1', 'tools/build_receipt.ps1', 'assets/kikiemu.ico') | ForEach-Object { Join-Path $repoRoot $_ }
$snapshot = Get-KikiBuildSnapshot -RepoRoot $repoRoot -SourceFiles $sourceInputs
foreach ($library in @('libarchive.a', 'libexpat.a')) {
    if (-not (Test-Path -LiteralPath (Join-Path $prefix "lib/$library"))) { throw "Missing native static dependency: $library" }
}
# MSYS2's static libarchive itself imports zlib. Link its official import
# library explicitly and ship that native dependency beside our binaries;
# never invent import aliases or require the user's MSYS2/PATH at runtime.
$zlib = Join-Path $prefix 'bin/zlib1.dll'
if (-not (Test-Path -LiteralPath $zlib)) { throw 'Missing native zlib runtime.' }
Copy-Item -LiteralPath $zlib -Destination (Join-Path $OutputDirectory 'zlib1.dll') -Force
$contractRoot = Join-Path $repoRoot 'src/kikiemu/contracts'
$pin = Get-Content -LiteralPath (Join-Path $contractRoot 'PIN.json') -Raw | ConvertFrom-Json
foreach ($record in $pin.files.PSObject.Properties) {
    if ((Get-FileHash -LiteralPath (Join-Path $contractRoot $record.Name) -Algorithm SHA256).Hash.ToLowerInvariant() -ne $record.Value) {
        throw "Producer contract bytes changed without a pinned update: $($record.Name)"
    }
}
$manifestSchema = [IO.File]::ReadAllText((Join-Path $contractRoot 'format-1/manifest.schema.json'))
$lockSchema = [IO.File]::ReadAllText((Join-Path $contractRoot 'format-1/source-lock.schema.json'))
$fixtureJson = [IO.File]::ReadAllText((Join-Path $contractRoot 'format-1/fixtures.json'))
if ($manifestSchema.Contains(')kikischema"') -or $lockSchema.Contains(')kikischema"') -or $fixtureJson.Contains(')kikischema"')) { throw 'Unsupported generated-header delimiter in contract.' }
$header = "// GENERATED in build output from pinned producer files. Do not edit.`n#pragma once`nnamespace kiki_contract {`n"
$header += 'inline constexpr const char* manifestSchema = R"kikischema(' + $manifestSchema + ')kikischema";' + "`n"
$header += 'inline constexpr const char* sourceLockSchema = R"kikischema(' + $lockSchema + ')kikischema";' + "`n"
$header += 'inline constexpr const char* fixtures = R"kikischema(' + $fixtureJson + ')kikischema";' + "`n"
$header += 'inline constexpr const char* manifestSha256 = "' + $pin.files.'format-1/manifest.schema.json' + '";' + "`n"
$header += 'inline constexpr const char* sourceLockSha256 = "' + $pin.files.'format-1/source-lock.schema.json' + '";' + "`n"
$header += 'inline constexpr const char* producerRevision = "' + $pin.revision + '";' + "`n}`n"
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'package_contract.generated.hpp'), $header, [Text.UTF8Encoding]::new($false))
$core = @('config', 'runtime', 'process', 'child', 'transport', 'boot', 'disk', 'storage', 'lifecycle', 'registry', 'schema', 'package', 'manager', 'session', 'installation') | ForEach-Object { Join-Path $repoRoot "src/kikiemu/$_.cpp" }
$flags = @('-std=c++20', '-O2', '-Wall', '-Wextra', '-static', '-municode', '-I', $OutputDirectory)
$libs = @('-larchive', '-l:libz.dll.a', '-lbz2', '-llzma', '-lb2', '-llz4', '-lzstd', '-lcrypto', '-liconv', '-lcharset', '-lexpat', '-lpcre2-posix', '-lpcre2-8', '-lcrypt32', '-lbcrypt', '-lole32', '-lshell32', '-luuid', '-liphlpapi', '-lws2_32')
# Compile the shared core once, then reuse it for every entry point.
$objects = @()
foreach ($source in $core) {
    $object = Join-Path $OutputDirectory (([IO.Path]::GetFileNameWithoutExtension($source)) + '.o')
    & $compiler @flags -c $source -o $object
    if ($LASTEXITCODE -ne 0) { throw "Native shared-core build failed: $source" }
    $objects += $object
}
$resources = Join-Path $OutputDirectory 'app-resource.o'
Push-Location $repoRoot
try {
    & (Join-Path $prefix 'bin/windres.exe') -I $repoRoot -i src/kikiemu/app.rc -o $resources
    if ($LASTEXITCODE -ne 0) { throw 'Native application icon/manifest resource build failed.' }
} finally { Pop-Location }
$cli = Join-Path $OutputDirectory 'kikiemu.exe'
& $compiler @flags -c (Join-Path $repoRoot 'src/kikiemu/cli.cpp') -o (Join-Path $OutputDirectory 'cli.o')
if ($LASTEXITCODE -ne 0) { throw 'Native CLI object build failed.' }
& $compiler @flags @objects $resources (Join-Path $OutputDirectory 'cli.o') -o $cli @libs
if ($LASTEXITCODE -ne 0) { throw 'Native manager-CLI build failed.' }
$desktop = Join-Path $OutputDirectory 'kikiemu-desktop.exe'
& $compiler @flags -c (Join-Path $repoRoot 'src/kikiemu/desktop.cpp') -o (Join-Path $OutputDirectory 'desktop.o')
if ($LASTEXITCODE -ne 0) { throw 'Native desktop object build failed.' }
& $compiler @flags -mwindows @objects $resources (Join-Path $OutputDirectory 'desktop.o') -o $desktop @libs
if ($LASTEXITCODE -ne 0) { throw 'Native desktop-supervisor build failed.' }
$setupHelper = Join-Path $OutputDirectory 'kikiemu-setup-helper.exe'
& $compiler @flags -c (Join-Path $repoRoot 'src/kikiemu/setup_helper.cpp') -o (Join-Path $OutputDirectory 'setup_helper.o')
if ($LASTEXITCODE -ne 0) { throw 'Native setup-helper object build failed.' }
& $compiler @flags -mwindows (Join-Path $OutputDirectory 'installation.o') (Join-Path $OutputDirectory 'setup_helper.o') $resources -o $setupHelper -lshell32 -lole32 -luuid -ladvapi32
if ($LASTEXITCODE -ne 0) { throw 'Native private setup helper build failed.' }
$inspector = Join-Path $OutputDirectory 'kikiemu-runtime-inspect.exe'
& $compiler @flags @objects (Join-Path $repoRoot 'src/kikiemu/runtime_inspect.cpp') -o $inspector @libs
if ($LASTEXITCODE -ne 0) { throw 'Native runtime-inspector build failed.' }
$tests = Join-Path $OutputDirectory 'kikiemu-core-tests.exe'
& $compiler @flags @objects (Join-Path $repoRoot 'tests/core_tests.cpp') -o $tests @libs
if ($LASTEXITCODE -ne 0) { throw 'Native core-unit-test build failed.' }
$diskPrototype = Join-Path $OutputDirectory 'kikiemu-disk-prototype.exe'
& $compiler @flags @objects (Join-Path $repoRoot 'src/kikiemu/disk_prototype.cpp') -o $diskPrototype @libs
if ($LASTEXITCODE -ne 0) { throw 'Native system-disk-prototype build failed.' }
$systemRegression = Join-Path $OutputDirectory 'kikiemu-system-regression.exe'
& $compiler @flags @objects $resources (Join-Path $repoRoot 'tests/system_regression.cpp') -o $systemRegression @libs
if ($LASTEXITCODE -ne 0) { throw 'Native internal real-system-regression runner build failed.' }
Write-Output "Built native ARM64 manager component and development tools: $OutputDirectory"
Write-Output 'This does not build/install setup.exe, register an instance, change PATH or start QEMU.'
if ($RunUnitTests) {
    if ($ProducerFixture) { & $tests --producer-package-fixture (Resolve-Path -LiteralPath $ProducerFixture).Path }
    else { & $tests }
    if ($LASTEXITCODE -ne 0) { throw 'Core unit tests failed.' }
}
Complete-KikiBuildReceipt -RepoRoot $repoRoot -Snapshot $snapshot -Compiler $compiler -Kind 'kikiemu-core' -Path $receiptPath `
    -OutputFiles @($cli, $desktop, $setupHelper, (Join-Path $OutputDirectory 'zlib1.dll')) `
    -UnitTests $(if ($RunUnitTests) { 'passed' } else { 'not-run' })
