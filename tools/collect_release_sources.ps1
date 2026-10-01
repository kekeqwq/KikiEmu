#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory)][string]$Msys2,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Proxy
)
$ErrorActionPreference = 'Stop'
$msysRoot = (Resolve-Path -LiteralPath $Msys2).Path
$output = [IO.Path]::GetFullPath($OutputDirectory)
if (Test-Path -LiteralPath $output) { throw 'Source collection requires a NEW output directory.' }
[void][IO.Directory]::CreateDirectory($output)
$required = @('libarchive','zlib','bzip2','xz','libb2','lz4','zstd','openssl','libiconv','expat','pcre2',
    'nlohmann-json','libc++','libunwind','compiler-rt','crt','headers','cppwinrt')
$records = [Collections.Generic.List[object]]::new()
$downloaded = @{}
$tar = Join-Path $msysRoot 'usr/bin/bsdtar.exe'
if (-not (Test-Path -LiteralPath $tar -PathType Leaf)) { throw 'MSYS2 bsdtar is required to validate source archives.' }
$requestOptions = @{TimeoutSec=1800}
if ($Proxy) { $requestOptions.Proxy=$Proxy }
function Assert-SourceArchive([string]$Path) {
    $entries = @(& $tar -tf $Path 2>&1)
    if ($LASTEXITCODE -ne 0 -or $entries.Count -lt 3) {
        throw "Downloaded file is not a valid source archive (possibly an HTML download page): $Path"
    }
}
foreach ($name in $required) {
    $installedPackages = @(Get-ChildItem -LiteralPath (Join-Path $msysRoot 'var/lib/pacman/local') -Directory |
        Where-Object Name -match ('^mingw-w64-clang-aarch64-' + [regex]::Escape($name) + '-[0-9]'))
    if ($installedPackages.Count -ne 1) { throw "Could not identify the exact installed package for $name." }
    $metadataPath = Join-Path $installedPackages[0].FullName 'desc'
    $lines = @(Get-Content -LiteralPath $metadataPath)
    function Field([string]$Label) {
        $index = [Array]::IndexOf($lines, "%$Label%")
        if ($index -lt 0 -or $index + 1 -ge $lines.Count) { throw "Missing package metadata: $Label" }
        return $lines[$index + 1]
    }
    $base = Field 'BASE'; $version = Field 'VERSION'; $package = Field 'NAME'
    if ($base -notmatch '^mingw-w64-[A-Za-z0-9_+.-]+$' -or $version -notmatch '^[A-Za-z0-9_+.:~-]+$') {
        throw 'Unsupported source-package metadata.'
    }
    $filename = "$base-$($version -replace '^[0-9]+:', '').src.tar.zst"
    $destination = Join-Path $output $filename
    $url = "https://repo.msys2.org/mingw/sources/$filename"
    if (-not $downloaded.ContainsKey($filename)) {
        Write-Host "Downloading exact installed dependency source: $filename"
        Invoke-WebRequest -Uri $url -OutFile $destination @requestOptions
        Assert-SourceArchive $destination
        $downloaded[$filename] = (Get-FileHash -LiteralPath $destination).Hash.ToLowerInvariant()
    }
    $records.Add([ordered]@{package=$package; version=$version; base=$base; sourceArchive=$filename;
        sourceUrl=$url; sha256=$downloaded[$filename]; packageMetadataSha256=(Get-FileHash $metadataPath).Hash.ToLowerInvariant()})
}
$nsisName = 'nsis-3.13-src.tar.bz2'
$nsisPath = Join-Path $output $nsisName
Write-Host 'Downloading the matching NSIS 3.13 bootstrap source.'
Invoke-WebRequest 'https://downloads.sourceforge.net/project/nsis/NSIS%203/3.13/nsis-3.13-src.tar.bz2' -OutFile $nsisPath @requestOptions
Assert-SourceArchive $nsisPath
$records.Add([ordered]@{package='nsis'; version='3.13'; sourceArchive=$nsisName;
    sourceUrl='https://downloads.sourceforge.net/project/nsis/NSIS%203/3.13/nsis-3.13-src.tar.bz2';
    sha256=(Get-FileHash -LiteralPath $nsisPath).Hash.ToLowerInvariant()})
[IO.File]::WriteAllText((Join-Path $output 'dependency-sources.json'), ($records | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
Write-Host "Dependency source archives collected: $output"
Write-Host 'Review archive contents, build receipts and linked components before packaging/publication.'
