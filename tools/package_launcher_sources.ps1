#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory)][string]$Msys2,
    [Parameter(Mandatory)][string]$CoreDirectory,
    [Parameter(Mandatory)][string]$SetupDirectory,
    [Parameter(Mandatory)][string]$DependencyDirectory,
    [Parameter(Mandatory)][string]$OutputDirectory
)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$revision=(& git -C $repo rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or (& git -C $repo status --porcelain --untracked-files=no)) { throw 'Source packaging requires committed, clean tracked inputs.' }
$core=(Resolve-Path -LiteralPath $CoreDirectory).Path
$setup=(Resolve-Path -LiteralPath $SetupDirectory).Path
$dependencies=(Resolve-Path -LiteralPath $DependencyDirectory).Path
$candidate=Get-Content -LiteralPath (Join-Path $setup 'candidate.json') -Raw | ConvertFrom-Json
if ($candidate.sourceCommit -ne $revision -or $candidate.setupSha256 -ne (Get-FileHash (Join-Path $setup 'setup.exe')).Hash.ToLowerInvariant()) { throw 'Installer does not match source revision/bytes.' }
$prefix=Join-Path (Resolve-Path -LiteralPath $Msys2).Path 'clangarm64'
$output=[IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Source packaging requires a NEW output directory.' }
$kit=Join-Path $output 'source-kit'
[void][IO.Directory]::CreateDirectory($kit)
$tar=Join-Path (Split-Path $prefix -Parent) 'usr/bin/bsdtar.exe'
$records=Get-Content -LiteralPath (Join-Path $dependencies 'dependency-sources.json') -Raw | ConvertFrom-Json
foreach ($record in $records) {
    $file=Join-Path $dependencies $record.sourceArchive
    if ((Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() -ne $record.sha256) { throw "Dependency source hash mismatch: $file" }
    $entries=@(& $tar -tf $file 2>&1)
    if ($LASTEXITCODE -ne 0 -or $entries.Count -lt 3) { throw "Invalid source archive: $file" }
    if ($record.package -ne 'nsis') {
        $metadata=Join-Path (Split-Path $prefix -Parent) "var/lib/pacman/local/$($record.package)-$($record.version)/desc"
        if ((Get-FileHash -LiteralPath $metadata).Hash.ToLowerInvariant() -ne $record.packageMetadataSha256) {
            throw 'Installed dependency changed since source collection; recollect/rebuild rather than mix inputs.'
        }
    }
}
Copy-Item -LiteralPath $dependencies -Destination (Join-Path $kit 'dependencies') -Recurse
Copy-Item -LiteralPath (Join-Path $setup 'payload/licenses') -Destination (Join-Path $kit 'licenses') -Recurse
Copy-Item -LiteralPath (Join-Path $repo 'tools/relink_launcher.ps1') -Destination $kit
$own=Join-Path $kit 'KikiEmu-source.zip'
& git -C $repo archive --format=zip -o $own $revision
if ($LASTEXITCODE -ne 0) { throw 'Project source export failed.' }
foreach ($directory in @('objects','libraries','runtime')) { [void][IO.Directory]::CreateDirectory((Join-Path $kit $directory)) }
$objects=@('config','runtime','process','child','transport','boot','disk','storage','lifecycle','registry','schema','package','manager','session','installation','cli','desktop','setup_helper','app-resource','surface_camera_bridge','surface_camera_winrt')
foreach ($name in $objects) { Copy-Item -LiteralPath (Join-Path $core "$name.o") -Destination (Join-Path $kit 'objects') }
foreach ($name in @('archive','bz2','lzma','b2','lz4','zstd','crypto','iconv','charset','expat','pcre2-posix','pcre2-8')) {
    Copy-Item -LiteralPath (Join-Path $prefix "lib/lib$name.a") -Destination (Join-Path $kit 'libraries')
}
Copy-Item -LiteralPath (Join-Path $prefix 'lib/libz.dll.a') -Destination (Join-Path $kit 'libraries')
Copy-Item -LiteralPath (Join-Path $core 'zlib1.dll') -Destination (Join-Path $kit 'runtime')
# Public provenance contains no developer home paths or credentials.
$provenance=[ordered]@{version='0.2.0-alpha'; sourceCommit=$revision; compiler=$candidate.buildReceipts.core.compiler;
    target='aarch64-w64-windows-gnu'; setupSha256=$candidate.setupSha256; sourceArchiveSha256=(Get-FileHash $own).Hash.ToLowerInvariant();
    dependencies=$records; objects=@{}; libraries=@{}}
foreach ($section in @('objects','libraries')) {
    foreach ($file in Get-ChildItem -LiteralPath (Join-Path $kit $section) -File) { $provenance[$section][$file.Name]=(Get-FileHash $file.FullName).Hash.ToLowerInvariant() }
}
[IO.File]::WriteAllText((Join-Path $kit 'source-provenance.json'),($provenance|ConvertTo-Json -Depth 12)+"`n")
Copy-Item -LiteralPath (Join-Path $repo 'LICENSING.md') -Destination $kit
$archive=Join-Path $output 'KikiEmu-0.2.0-alpha-source-kit.tar.gz'
& $tar -czf $archive -C $output source-kit
if ($LASTEXITCODE -ne 0) { throw 'Source kit compression failed.' }
Write-Host "Source, dependency archives and relinkable application objects: $archive"
