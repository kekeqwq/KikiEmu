# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory)][string]$CoreDirectory,
    [Parameter(Mandatory)][string]$NsisDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'build_receipt.ps1')
$core = (Resolve-Path -LiteralPath $CoreDirectory).Path
$nsis = (Resolve-Path -LiteralPath $NsisDirectory).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Use a NEW candidate output directory. Existing releases are never overwritten.' }
$compiler = (Get-Command clang++ -ErrorAction Stop).Source
$prefix = Split-Path (Split-Path $compiler -Parent) -Parent
$readobj = Join-Path $prefix 'bin/llvm-readobj.exe'
if ((& $compiler -dumpmachine).Trim() -ne 'aarch64-w64-windows-gnu') { throw 'Use native MSYS2 CLANGARM64.' }
$commit = (& git -C $repoRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or (& git -C $repoRoot status --porcelain --untracked-files=no)) { throw 'Commit all tracked source changes before packaging.' }
$coreReceipt = Assert-KikiBuildReceipt -Path (Join-Path $core 'core-build-receipt.json') -Kind 'kikiemu-core' -SourceCommit $commit `
    -OutputDirectory $core -OutputNames @('kikiemu.exe', 'kikiemu-desktop.exe', 'kikiemu-setup-helper.exe', 'zlib1.dll') -RequireUnitTests
$cameraReceipt = Assert-KikiBuildReceipt -Path (Join-Path $core 'surface-camera-bridge.exe.build-receipt.json') -Kind 'surface-camera' `
    -SourceCommit $commit -OutputDirectory $core -OutputNames @('surface-camera-bridge.exe')
$make = Join-Path $nsis 'makensis.exe'
if ((& $make /VERSION).Trim() -ne 'v3.13') { throw 'Use the pinned standard NSIS 3.13 toolchain.' }
$files = @('kikiemu.exe', 'kikiemu-desktop.exe', 'kikiemu-setup-helper.exe', 'surface-camera-bridge.exe', 'zlib1.dll')
foreach ($name in $files) {
    $path = Join-Path $core $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing built product: $name" }
    $inspection = (& $readobj --file-headers --coff-imports $path) -join "`n"
    if ($LASTEXITCODE -ne 0 -or $inspection -notmatch 'Machine: IMAGE_FILE_MACHINE_ARM64') { throw "Product must be native ARM64: $name" }
    foreach ($match in [regex]::Matches($inspection, '(?m)^\s*Name: ([^\r\n]+[.]dll)\s*$')) {
        $dll = $match.Groups[1].Value.ToLowerInvariant()
        if ($dll -notmatch '^(api-ms-|ext-ms-)' -and $dll -notin @('kernel32.dll', 'user32.dll', 'advapi32.dll', 'ole32.dll', 'oleaut32.dll', 'shell32.dll', 'bcrypt.dll', 'crypt32.dll', 'ntdll.dll', 'ws2_32.dll', 'iphlpapi.dll', 'mf.dll', 'mfplat.dll', 'mfreadwrite.dll', 'propsys.dll', 'combase.dll', 'ucrtbase.dll', 'zlib1.dll')) {
            throw "Unbundled/non-system runtime dependency in $name`: $dll"
        }
    }
    if ($name -in @('kikiemu-desktop.exe', 'kikiemu-setup-helper.exe') -and $inspection -notmatch 'Subsystem: IMAGE_SUBSYSTEM_WINDOWS_GUI') { throw 'Desktop/private helper must not create consoles.' }
}
$payload = Join-Path $output 'payload'
New-Item -ItemType Directory -Path $payload -Force | Out-Null
foreach ($name in $files) { Copy-Item -LiteralPath (Join-Path $core $name) -Destination $payload }
foreach ($name in @('LICENSE', 'LICENSE_NOTICE.md', 'QUICK_START.md')) { Copy-Item -LiteralPath (Join-Path $repoRoot $name) -Destination $payload }
Copy-Item -LiteralPath (Join-Path $repoRoot 'assets/kikiemu.ico') -Destination $payload
$licenseRoot = Join-Path $payload 'licenses'
New-Item -ItemType Directory -Path $licenseRoot | Out-Null
$projectLicenses = Join-Path $licenseRoot 'project'
New-Item -ItemType Directory -Path $projectLicenses | Out-Null
Copy-Item -LiteralPath (Join-Path $repoRoot 'LICENSES/GPL-3.0.txt') -Destination $projectLicenses
$dependencies = @('json', 'libarchive', 'zlib', 'bzip2', 'xz', 'libb2', 'zstd', 'libiconv', 'expat', 'pcre2', 'openssl', 'libc++', 'libunwind', 'compiler-rt', 'crt', 'headers', 'cppwinrt')
foreach ($dependency in $dependencies) {
    $source = Join-Path $prefix "share/licenses/$dependency"
    if (-not (Test-Path -LiteralPath $source -PathType Container)) { throw "Missing official dependency license: $dependency" }
    Copy-Item -LiteralPath $source -Destination (Join-Path $licenseRoot $dependency) -Recurse
}
$lz4 = Join-Path $licenseRoot 'lz4'
New-Item -ItemType Directory -Path $lz4 | Out-Null
Copy-Item -LiteralPath (Join-Path $prefix 'include/lz4.h') -Destination $lz4
$nsisLicense = Join-Path $licenseRoot 'nsis'
New-Item -ItemType Directory -Path $nsisLicense | Out-Null
Copy-Item -LiteralPath (Join-Path $nsis 'COPYING') -Destination $nsisLicense
# All of these paths were compiler-enumerated from a NEW payload tree, never
# a caller-selected instance. No wildcard/recursive storage deletion script.
$uninstall = @()
foreach ($file in Get-ChildItem -LiteralPath $licenseRoot -Recurse -File) {
    $relative = [IO.Path]::GetRelativePath($payload, $file.FullName)
    if ($relative -match '["$\r\n]') { throw 'Unsafe generated license filename.' }
    $uninstall += 'Delete "$INSTDIR\' + $relative + '"'
}
foreach ($directory in (Get-ChildItem -LiteralPath $licenseRoot -Recurse -Directory | Sort-Object { $_.FullName.Length } -Descending)) {
    $relative = [IO.Path]::GetRelativePath($payload, $directory.FullName)
    $uninstall += 'RMDir "$INSTDIR\' + $relative + '"'
}
[IO.File]::WriteAllLines((Join-Path $payload 'uninstall-licenses.nsh'), $uninstall, [Text.UTF8Encoding]::new($false))
$setup = Join-Path $output 'setup.exe'
& $make /WX /V3 "/DPAYLOAD=$payload" "/DOUTPUT=$setup" (Join-Path $repoRoot 'installer/kikiemu.nsi')
if ($LASTEXITCODE -ne 0) { throw 'NSIS compilation failed. Generated setup was NOT executed.' }
$record = [ordered]@{ version = '0.3.1-alpha'; status = 'compiled-candidate-not-user-accepted'; sourceCommit = $commit;
    buildReceipts = @{ core = $coreReceipt; camera = $cameraReceipt }; combinedBinaryLicense = 'GPL-3.0';
    installerStub = 'x86-unicode'; installedApplications = 'native-arm64'; payload = @{}; setupSha256 = (Get-FileHash -LiteralPath $setup).Hash.ToLowerInvariant() }
foreach ($file in Get-ChildItem -LiteralPath $payload -Recurse -File) {
    $record.payload[[IO.Path]::GetRelativePath($payload, $file.FullName)] = @{ bytes = $file.Length; sha256 = (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() }
}
[IO.File]::WriteAllText((Join-Path $output 'candidate.json'), ($record | ConvertTo-Json -Depth 10) + "`n", [Text.UTF8Encoding]::new($false))
Write-Output "Compiled candidate: $setup"
Write-Output 'Not executed/installed. No PATH/shortcut/user instance changes. System ZIP and acceptance are separate.'
