# SPDX-License-Identifier: GPL-2.0-or-later
# Local publication-plan validation only: NO credentials/network/release writes.
$ErrorActionPreference='Stop'
$tool=(Resolve-Path (Join-Path $PSScriptRoot '../tools/publish_alpha_release.ps1')).Path
$root=Join-Path ([IO.Path]::GetTempPath()) ('kiki-release-plan-'+[guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($root)
foreach ($name in @('setup.exe','KikiAOSP-0.1.0-alpha-arm64.zip','source-kit.tar.gz','release-provenance.json','notes.md')) {
    [IO.File]::WriteAllText((Join-Path $root $name),'INERT VALIDATION FIXTURE: not executable or a bootable system')
}
$plan=Join-Path $root 'plan.json'
function Make-Plan {
    $releases=@()
    foreach ($repo in @('kekeqwq/KikiEmu','kekeqwq/kikiaosp_test')) {
        $primary=if ($repo -eq 'kekeqwq/KikiEmu') {'setup.exe'} else {'KikiAOSP-0.1.0-alpha-arm64.zip'}
        $assets=@($primary,'source-kit.tar.gz','release-provenance.json') | ForEach-Object {
            $file=Get-Item -LiteralPath (Join-Path $root $_)
            @{path=$file.FullName;bytes=$file.Length;sha256=(Get-FileHash $file.FullName).Hash.ToLowerInvariant()}
        }
        $releases+=@{repository=$repo;tag='v0.1.0-alpha';commit=('a'*40);notes=(Join-Path $root 'notes.md');name='Alpha';assets=$assets}
    }
    return @{version='0.1.0-alpha';releases=$releases}
}
$passed=0
function Check($value,[bool]$reject) {
    [IO.File]::WriteAllText($plan,($value|ConvertTo-Json -Depth 10))
    $failed=$false
    try { & $tool -Plan $plan -JournalDirectory (Join-Path $root 'unused-journal') -Mode Validate }
    catch { $failed=$true }
    if ($failed -ne $reject) { throw 'Publication-plan validation returned the wrong result.' }
    if (Test-Path -LiteralPath (Join-Path $root 'unused-journal')) { throw 'Validation created a publication journal.' }
    $script:passed++
}
Check (Make-Plan) $false
$v=Make-Plan; $v.releases[0].assets[0].sha256='b'*64; Check $v $true
$v=Make-Plan; $v.releases[0].assets[0].bytes++; Check $v $true
$v=Make-Plan; $v.releases[1].repository='foreign/repo'; Check $v $true
$v=Make-Plan; $v.releases[1].repository='kekeqwq/KikiEmu'; Check $v $true
$v=Make-Plan; $v.releases[0].tag='main'; Check $v $true
$v=Make-Plan; $v.releases[0].assets=@(); Check $v $true
$v=Make-Plan; $v.releases[0].commit='main'; Check $v $true
Write-Host "PASS: $passed isolated publication-plan checks; no network, credentials, releases, installer or public CLI accessed."
# Retain this small inert fixture for diagnostics; never recursively delete a user path.
