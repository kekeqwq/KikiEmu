# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory)][string]$BuildDirectory,
    [Parameter(Mandatory)][string]$OutputBin,
    [string]$DependencyBin = 'C:/msys64/clangarm64/bin',
    [string]$InspectorPath
)
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
if (-not $InspectorPath) { $InspectorPath = Join-Path $repoRoot 'build/kikiemu-core/kikiemu-runtime-inspect.exe' }
$inspector = (Resolve-Path -LiteralPath $InspectorPath).Path
$source = (Resolve-Path -LiteralPath $BuildDirectory).Path
$dependencySource = (Resolve-Path -LiteralPath $DependencyBin).Path
$destination = [IO.Path]::GetFullPath($OutputBin)
if (Test-Path -LiteralPath $destination) { throw 'OutputBin must be a new directory. Never overwrite an in-use runtime.' }
$jsonText = & $inspector $source $dependencySource
if ($LASTEXITCODE -ne 0) { throw 'QEMU build/dependency inspection failed; no export was created.' }
$inspection = ($jsonText -join "`n") | ConvertFrom-Json
New-Item -ItemType Directory -Path $destination | Out-Null
try {
    foreach ($file in $inspection.files) {
        # Inputs are a reviewed local build, not package-supplied host commands.
        $target = Join-Path $destination $file.name
        Copy-Item -LiteralPath $file.source -Destination $target
        if ((Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant() -ne $file.sha256) {
            throw "Export hash mismatch: $($file.name)"
        }
    }
    # qemu -L is always the configured bin's roms directory, not an implicit
    # dependency on the source tree. Keep export output independent of builds.
    $bios = Join-Path (Split-Path $source -Parent) 'pc-bios'
    if (-not (Test-Path -LiteralPath $bios -PathType Container)) { throw 'QEMU pc-bios directory is missing.' }
    $roms = Join-Path $destination 'roms'
    New-Item -ItemType Directory -Path $roms | Out-Null
    Get-ChildItem -LiteralPath $bios -File | Where-Object { $_.Extension -in '.bin', '.rom', '.fd', '.dtb' } |
        ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $roms }
    $privateInspection = & $inspector $destination
    if ($LASTEXITCODE -ne 0) { throw 'Exported QEMU is not self-contained.' }
    [IO.File]::WriteAllText((Join-Path $destination 'qemu-binding.json'), ($privateInspection -join "`n") + "`n", [Text.UTF8Encoding]::new($false))
    Write-Output "Exported native ARM64 QEMU and static DLL closure: $destination"
    Write-Output 'This is a development export, not public-release acceptance. Host GL, dynamically loaded modules and boot ABI still need verification.'
} catch {
    # Do not recursively remove a user path. Leave visibly incomplete output
    # for inspection; no manager/default is registered on an export failure.
    throw "QEMU export is incomplete at '$destination': $($_.Exception.Message)"
}
