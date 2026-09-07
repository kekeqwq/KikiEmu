# KikiEmu Build & Publish Script for Windows on ARM (ARM64)
$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$outDir = Join-Path $root "dist"

Write-Host "==> Publishing KikiEmu for win-arm64..." -ForegroundColor Cyan

# Publish CLI
Write-Host "Publishing KikiEmu.Cli..." -ForegroundColor Yellow
dotnet publish "$root/src/KikiEmu.Cli/KikiEmu.Cli.csproj" -c Release -r win-arm64 --self-contained true -o "$outDir" /p:PublishSingleFile=true /p:IncludeNativeLibrariesForSelfExtract=true

# Rename to kikiemu.exe for convenience
if (Test-Path "$outDir/KikiEmu.Cli.exe") {
    Copy-Item "$outDir/KikiEmu.Cli.exe" "$outDir/kikiemu.exe" -Force
}

# Publish UI
Write-Host "Publishing KikiEmu.UI..." -ForegroundColor Yellow
dotnet publish "$root/src/KikiEmu.UI/KikiEmu.UI.csproj" -c Release -r win-arm64 --self-contained true -o "$outDir/ui" /p:PublishSingleFile=true /p:IncludeNativeLibrariesForSelfExtract=true

Write-Host "==> Build complete! Output located at: $outDir" -ForegroundColor Green
Write-Host "You can add $outDir to your PATH to run 'kikiemu' from any terminal." -ForegroundColor Cyan
