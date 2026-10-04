# SPDX-License-Identifier: GPL-2.0-or-later
# Developer tools only. Reuses the audited native shared-core objects supplied
# by the caller, not a user installation. Never installs or changes host state.
param(
    [Parameter(Mandatory)][string]$NativeDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Msys2Root='C:\msys64'
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
$native=(Resolve-Path $NativeDirectory).Path
$out=[IO.Path]::GetFullPath($OutputDirectory)
if(Test-Path $out){throw 'Use a NEW developer tool output directory'}
$receipt=Join-Path $native 'core-build-receipt.json'
if(!(Test-Path $receipt)){throw 'Audited native core build receipt required'}
$names=@('config','runtime','process','child','transport','boot','disk','storage','lifecycle','registry','schema','package','manager','session','installation')
$objects=$names|ForEach-Object { $p=Join-Path $native ($_+'.o');if(!(Test-Path $p)){throw "Missing core object: $p"};$p }
$compiler=Join-Path $Msys2Root 'clangarm64/bin/clang++.exe'
$libs=@('-larchive','-l:libz.dll.a','-lbz2','-llzma','-lb2','-llz4','-lzstd','-lcrypto','-liconv','-lcharset','-lexpat','-lpcre2-posix','-lpcre2-8','-lcrypt32','-lbcrypt','-lole32','-lshell32','-luuid','-liphlpapi','-lws2_32')
New-Item -ItemType Directory -Path $out|Out-Null
$oldPath=$env:PATH
try {
    $env:PATH=(Join-Path $Msys2Root 'clangarm64/bin')+';'+$oldPath
    foreach($tool in @(@('process_loopback.cpp','process-loopback.exe'),@('guest_stage.cpp','guest-stage.exe'))) {
        & $compiler -std=c++20 -O2 -Wall -Wextra -static -municode (Join-Path $PSScriptRoot $tool[0]) @objects -o (Join-Path $out $tool[1]) @libs
        if($LASTEXITCODE -ne 0){throw "Native audio-test build failed: $($tool[0])"}
    }
    Copy-Item (Join-Path $native 'zlib1.dll') $out
    Copy-Item $receipt (Join-Path $out 'input-core-build-receipt.json')
    $hashes=foreach($p in @($objects)+@((Join-Path $out 'process-loopback.exe'),(Join-Path $out 'guest-stage.exe'))) { [ordered]@{path=$p;sha256=(Get-FileHash $p -Algorithm SHA256).Hash.ToLowerInvariant()} }
    $hashes|ConvertTo-Json -Depth 4|Set-Content -Encoding utf8 (Join-Path $out 'build-hashes.json')
} finally {$env:PATH=$oldPath}
