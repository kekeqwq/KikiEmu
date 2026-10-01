# SPDX-License-Identifier: GPL-2.0-or-later
# Isolated receipt fixtures only. No setup/public CLI/VM/host configuration.
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot '../tools/build_receipt.ps1')
$taskFixture = Join-Path ([IO.Path]::GetTempPath()) ('kikiemu-receipt-tests-' + [guid]::NewGuid().ToString('D'))
New-Item -ItemType Directory -Path $taskFixture | Out-Null
$binary = Join-Path $taskFixture 'fixture.exe'
$receiptFile = Join-Path $taskFixture 'receipt.json'
$passed = 0
function Write-FixtureReceipt($record) {
    [IO.File]::WriteAllText($receiptFile, ($record | ConvertTo-Json -Depth 7), [Text.UTF8Encoding]::new($false))
}
function Assert-FixtureRejected($record, [string]$reason) {
    Write-FixtureReceipt $record
    $rejected = $false
    try { Assert-KikiBuildReceipt -Path $receiptFile -Kind 'kikiemu-core' -SourceCommit ('a' * 40) -OutputDirectory $taskFixture -OutputNames @('fixture.exe') -RequireUnitTests | Out-Null }
    catch { $rejected = $true }
    if (-not $rejected) { throw "Receipt accepted invalid fixture: $reason" }
    $script:passed++
}
try {
    [IO.File]::WriteAllText($binary, 'NOT AN EXECUTABLE: internal receipt fixture only', [Text.UTF8Encoding]::new($false))
    $valid = @{ receiptVersion = 1; kind = 'kikiemu-core'; status = 'compiled'; sourceCommit = ('a' * 40); sourceTreeClean = $true;
        sourceFiles = @{ 'fixture.cpp' = ('b' * 64) }; unitTests = 'passed';
        compiler = @{ target = 'aarch64-w64-windows-gnu'; sha256 = ('c' * 64) };
        outputs = @{ 'fixture.exe' = @{ bytes = (Get-Item -LiteralPath $binary).Length; sha256 = (Get-FileHash -LiteralPath $binary).Hash.ToLowerInvariant() } } }
    Write-FixtureReceipt $valid
    Assert-KikiBuildReceipt -Path $receiptFile -Kind 'kikiemu-core' -SourceCommit ('a' * 40) -OutputDirectory $taskFixture -OutputNames @('fixture.exe') -RequireUnitTests | Out-Null
    $passed++
    foreach ($case in @(
        @('sourceCommit', ('d' * 40)), @('sourceTreeClean', $false), @('sourceTreeClean', 'true'),
        @('kind', 'surface-camera'), @('status', 'building-not-distributable'), @('unitTests', 'not-run')
    )) {
        $wrong = ($valid | ConvertTo-Json -Depth 7) | ConvertFrom-Json -AsHashtable
        $wrong[$case[0]] = $case[1]; Assert-FixtureRejected $wrong $case[0]
    }
    $wrong = ($valid | ConvertTo-Json -Depth 7) | ConvertFrom-Json -AsHashtable
    $wrong.outputs.'fixture.exe'.sha256 = ('e' * 64); Assert-FixtureRejected $wrong 'stale output SHA'
    $wrong = ($valid | ConvertTo-Json -Depth 7) | ConvertFrom-Json -AsHashtable
    $wrong.outputs.'fixture.exe'.bytes++; Assert-FixtureRejected $wrong 'wrong byte count'
    $wrong = ($valid | ConvertTo-Json -Depth 7) | ConvertFrom-Json -AsHashtable
    $wrong.compiler.target = 'x86_64-w64-windows-gnu'; Assert-FixtureRejected $wrong 'wrong target architecture'
    $wrong = ($valid | ConvertTo-Json -Depth 7) | ConvertFrom-Json -AsHashtable
    $wrong.sourceFiles = @{}; Assert-FixtureRejected $wrong 'missing source inputs'
    Initialize-KikiBuildReceipt -Path $receiptFile -Kind 'kikiemu-core'
    $invalidated = [IO.File]::ReadAllText($receiptFile) | ConvertFrom-Json
    if ($invalidated.status -cne 'building-not-distributable') { throw 'New build did not invalidate the old receipt.' }
    $passed++
    "PASS: $passed isolated build-receipt checks. No executable was run or installed."
} finally {
    # Exact files created in this NEW UUID fixture. No recursive/broad cleanup.
    if (Test-Path -LiteralPath $binary) { Remove-Item -LiteralPath $binary }
    if (Test-Path -LiteralPath $receiptFile) { Remove-Item -LiteralPath $receiptFile }
    Remove-Item -LiteralPath $taskFixture
}
