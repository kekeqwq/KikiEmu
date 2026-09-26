[CmdletBinding()]
param(
  [string]$AospHost = 'keke@192.168.2.185',
  [string]$KernelHost = 'keke@192.168.2.185',
  [string]$AospRoot = '/home/keke/aosp-master',
  [string]$DeviceRepo = '/home/keke/projects/kikiaosp_test',
  [string]$KernelRepo = '/home/keke/projects/kikiaosp_kernel',
  [string]$OutputDir = (Join-Path $PSScriptRoot '..\bundles\launcher3-settings-20260926'),
  [string]$LocalSupportArchive
)

$ErrorActionPreference = 'Stop'
$profilePath = Join-Path $PSScriptRoot '..\profiles\launcher3-settings-20260926.json'
$profile = Get-Content -LiteralPath $profilePath -Raw | ConvertFrom-Json
$outputFull = [IO.Path]::GetFullPath($OutputDir)

function Assert-Hash([string]$path, $spec) {
  if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
    throw "Missing $($spec.name): $path"
  }
  $actualSize = (Get-Item -LiteralPath $path).Length
  if ($actualSize -ne [long]$spec.bytes) {
    throw "Size mismatch for $($spec.name): expected $($spec.bytes), got $actualSize"
  }
  $actualHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
  if ($actualHash -ne $spec.sha256) {
    throw "SHA-256 mismatch for $($spec.name): expected $($spec.sha256), got $actualHash"
  }
  Write-Host "Verified $($spec.name): $actualHash"
}

function Assert-RemoteCommit([string]$hostName, [string]$repoPath, [string]$expected) {
  $actual = (& ssh -o BatchMode=yes -o ConnectTimeout=10 $hostName "git -C '$repoPath' rev-parse HEAD").Trim()
  if ($LASTEXITCODE -ne 0) { throw "Cannot read remote checkout at ${hostName}:$repoPath" }
  if ($actual -ne $expected) {
    & ssh -o BatchMode=yes -o ConnectTimeout=10 $hostName "git -C '$repoPath' merge-base --is-ancestor '$expected' HEAD"
    if ($LASTEXITCODE -ne 0) {
      throw "Wrong remote history at ${hostName}:$repoPath. Required ancestor $expected, got $actual"
    }
  }
  Write-Host "Verified remote history ${hostName}:$repoPath (required $expected, HEAD $actual)"
}

function Receive-Asset([string]$hostName, [string]$remotePath, $spec) {
  $source = "${hostName}:$remotePath"
  $target = Join-Path $outputFull $spec.name
  Write-Host "Receiving $($spec.name) from $source"
  & scp $source $target
  if ($LASTEXITCODE -ne 0) { throw "SCP failed: $source" }
  Assert-Hash $target $spec
}

if (Test-Path -LiteralPath $outputFull) {
  throw "Output directory already exists; refusing to overwrite: $outputFull"
}
Assert-RemoteCommit $AospHost $DeviceRepo $profile.deviceRepositoryCommit
Assert-RemoteCommit $KernelHost $KernelRepo $profile.kernelRepositoryCommit

New-Item -ItemType Directory -Path $outputFull -Force | Out-Null
try {
  Receive-Asset $KernelHost "$KernelRepo/$($profile.kernel.source)" $profile.kernel
  Receive-Asset $AospHost "$AospRoot/$($profile.system.source)" $profile.system
  Receive-Asset $AospHost "$AospRoot/$($profile.vendor.source)" $profile.vendor

  $archivePath = Join-Path $outputFull $profile.supportArchive.name
  if ($LocalSupportArchive) {
    Write-Host "Copying frozen runtime support from $LocalSupportArchive"
    Copy-Item -LiteralPath $LocalSupportArchive -Destination $archivePath
    Assert-Hash $archivePath $profile.supportArchive
  } else {
    Receive-Asset $AospHost "$DeviceRepo/$($profile.supportArchive.source)" $profile.supportArchive
  }

  $actualEntries = @(& tar -tf $archivePath)
  if ($LASTEXITCODE -ne 0) { throw "Could not list support archive: $archivePath" }
  $expectedEntries = @($profile.supportFiles | ForEach-Object { $_.name })
  if (@(Compare-Object $expectedEntries $actualEntries).Count -ne 0) {
    throw "Support archive has unexpected or missing entries: $($actualEntries -join ', ')"
  }
  & tar -xf $archivePath -C $outputFull
  if ($LASTEXITCODE -ne 0) { throw "Could not extract support archive: $archivePath" }
  foreach ($spec in $profile.supportFiles) {
    Assert-Hash (Join-Path $outputFull $spec.name) $spec
  }
  Copy-Item -LiteralPath $profilePath -Destination (Join-Path $outputFull 'bundle-profile.json')
  Write-Host "Bundle ready: $outputFull"
  Write-Host "Start with: .\tools\run_kikiaosp_touch_local.ps1 -BundleDir '$outputFull'"
} catch {
  Write-Warning "Collection is incomplete; partial files were left for inspection at $outputFull"
  throw
}
