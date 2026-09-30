param(
    [string]$Tag = ('surface-camera-' + (Get-Date -Format 'yyyyMMdd-HHmmss')),
    [string]$BundleDir,
    [string]$QemuPath,
    [switch]$ValidateAllAssets,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$profile = Get-Content (Join-Path $repoRoot 'profiles/surface-camera-20260930.json') -Raw | ConvertFrom-Json
if (-not $BundleDir) { $BundleDir = Join-Path $repoRoot $profile.bundleDirectory }
if (-not $QemuPath) { $QemuPath = Join-Path $repoRoot $profile.qemuExe }
$BundleDir = (Resolve-Path -LiteralPath $BundleDir).Path
$QemuPath = (Resolve-Path -LiteralPath $QemuPath).Path
$assets = @($profile.kernel, $profile.system, $profile.vendor, $profile.product, $profile.systemExt) + @($profile.supportFiles)
foreach ($asset in $assets) {
    $path = Join-Path $BundleDir $asset.name
    $file = Get-Item -LiteralPath $path -ErrorAction Stop
    if ($file.Length -ne $asset.bytes) { throw "Asset size mismatch: $($asset.name)" }
    # Kernel/vendor hashes are cheap and protect the two changes tested here.
    # Full validation includes the large read-only system and base userdata.
    if ($ValidateAllAssets -or $asset -eq $profile.kernel -or $asset -eq $profile.vendor) {
        if ((Get-FileHash -LiteralPath $path).Hash.ToLowerInvariant() -ne $asset.sha256) {
            throw "Asset SHA-256 mismatch: $($asset.name)"
        }
    }
}
if ((Get-FileHash -LiteralPath $QemuPath).Hash.ToLowerInvariant() -ne $profile.testedQemuSha256) {
    Write-Warning 'This QEMU is not the byte-identical tested binary; the launcher will also check required SDL features. Rebuilt binaries require regression testing.'
}
& (Join-Path $PSScriptRoot 'run_kikiaosp_touch_local.ps1') -Tag $Tag -BundleDir $BundleDir -QemuPath $QemuPath `
    -KernelImage $profile.kernel.name -SystemImage $profile.system.name -VendorImage $profile.vendor.name `
    -ProductImage $profile.product.name -SystemExtImage $profile.systemExt.name `
    -SurfaceCameras -GpuMode Virgl -VcpuCount $profile.display.vcpu -GuestRefreshRateHz $profile.display.refreshRateHz `
    -PortraitWidthPixels $profile.display.width -PortraitHeightPixels $profile.display.height -DryRun:$DryRun
