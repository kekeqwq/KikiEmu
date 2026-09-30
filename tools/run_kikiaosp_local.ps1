param(
    [string]$Tag = ('surface-main-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$ProfilePath = (Join-Path $PSScriptRoot '../profiles/surface-main-20260930.json'),
    [string]$BundleDir,
    [string]$QemuPath,
    [switch]$SurfaceCameras = $true,
    [switch]$BootConsole,
    [switch]$ValidateAllAssets,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$profile = Get-Content -LiteralPath $ProfilePath -Raw | ConvertFrom-Json
if (-not $PSBoundParameters.ContainsKey('BootConsole')) {
    $BootConsole = [bool]$profile.display.bootConsole
}
if (-not $BundleDir) { $BundleDir = Join-Path $repoRoot $profile.bundleDirectory }
if (-not $QemuPath) { $QemuPath = Join-Path $repoRoot $profile.qemuExe }
$BundleDir = (Resolve-Path -LiteralPath $BundleDir).Path
$QemuPath = (Resolve-Path -LiteralPath $QemuPath).Path
$assets = @($profile.kernel, $profile.system, $profile.vendor, $profile.product, $profile.systemExt) + @($profile.supportFiles)
foreach ($asset in $assets) {
    $path = Join-Path $BundleDir $asset.name
    $file = Get-Item -LiteralPath $path -ErrorAction Stop
    if ($file.Length -ne $asset.bytes) { throw "Asset size mismatch: $($asset.name)" }
    # Check the actual OS/driver pairing on every launch; full validation also
    # hashes auxiliary disks, including the large frozen userdata base.
    if ($ValidateAllAssets -or $asset -eq $profile.kernel -or $asset -eq $profile.system -or $asset -eq $profile.vendor) {
        if ((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $asset.sha256) {
            throw "Asset SHA-256 mismatch: $($asset.name)"
        }
    }
}
if ((Get-FileHash -LiteralPath $QemuPath).Hash.ToLowerInvariant() -ne $profile.testedQemuSha256) {
    Write-Warning 'This QEMU is not the byte-identical tested binary; required SDL features will also be checked. Rebuilt binaries require regression testing.'
}
Write-Host "BASELINE_PROFILE=$($profile.id) SYSTEM_SHA256=$($profile.system.sha256) VENDOR_SHA256=$($profile.vendor.sha256)"
& (Join-Path $PSScriptRoot 'run_kikiaosp_touch_local.ps1') -Tag $Tag -BundleDir $BundleDir -QemuPath $QemuPath `
    -KernelImage $profile.kernel.name -SystemImage $profile.system.name -VendorImage $profile.vendor.name `
    -ProductImage $profile.product.name -SystemExtImage $profile.systemExt.name `
    -SurfaceCameras:$SurfaceCameras -GpuMode $profile.display.gpu -DisplayBackend $profile.display.backend `
    -VcpuCount $profile.display.vcpu -GuestRefreshRateHz $profile.display.refreshRateHz `
    -PortraitWidthPixels $profile.display.width -PortraitHeightPixels $profile.display.height `
    -BootConsole:$BootConsole -DryRun:$DryRun
