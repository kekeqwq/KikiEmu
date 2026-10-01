#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
<#
.SYNOPSIS
Prepare private runtime dependencies beside a completed native ARM64 QEMU build.
.EXAMPLE
./prepare.ps1 --msys2 'C:\msys64'
.EXAMPLE
./prepare.ps1 -Msys2 'C:\msys64' -BinDirectory 'D:\QemuRuntime\bin'
.NOTES
Run from the matching QEMU checkout root. Reads CLANGARM64's llvm-readobj,
copies only missing imported DLLs and pc-bios ROMs. Never builds, launches,
replaces EXEs, overwrites existing files or changes the host PATH.
No KikiEmu checkout or internal inspector is required.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Msys2,
    [string]$BinDirectory,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Options
)

function Resolve-KikiPrepareOptions {
    param([string]$Msys2Path, [string]$BinPath, [string[]]$ExtraOptions)
    $values = @{ '--msys2' = $Msys2Path; '--bin' = $BinPath }
    for ($i = 0; $i -lt $ExtraOptions.Count; $i++) {
        $option = $ExtraOptions[$i]
        if (-not $values.ContainsKey($option)) { throw "Unknown prepare option: $option" }
        if ($i + 1 -ge $ExtraOptions.Count) { throw "Missing value for $option." }
        if (-not [string]::IsNullOrWhiteSpace($values[$option])) { throw "Specify $option only once." }
        $values[$option] = $ExtraOptions[++$i]
        if ([string]::IsNullOrWhiteSpace($values[$option])) { throw "Missing value for $option." }
    }
    if ([string]::IsNullOrWhiteSpace($values['--msys2'])) {
        throw "Specify your MSYS2 installation: ./prepare.ps1 --msys2 'C:\msys64'"
    }
    [pscustomobject]@{ Msys2 = $values['--msys2']; Bin = $values['--bin'] }
}

function Get-KikiPreparePlainPath {
    param([string]$Path, [switch]$Directory)
    $item = Get-Item -LiteralPath $Path -Force -ErrorAction Stop
    if ($item.PSProvider.Name -ne 'FileSystem' -or $item.PSIsContainer -ne [bool]$Directory) {
        throw "Expected a regular $(if ($Directory) { 'directory' } else { 'file' }): $Path"
    }
    $current = $item
    while ($null -ne $current) {
        if ($current.Attributes -band [IO.FileAttributes]::ReparsePoint) {
            throw "Linked/reparse paths are not supported: $($current.FullName)"
        }
        $current = if ($current.PSIsContainer) { $current.Parent } else { $current.Directory }
    }
    return $item.FullName
}

function ConvertFrom-KikiPrepareImports {
    param([string]$Text, [string]$File)
    if ($Text -notmatch '(?im)^\s*Machine:\s*IMAGE_FILE_MACHINE_ARM64\s*\(0xAA64\)\s*$') {
        throw "Runtime file is not native ARM64: $File"
    }
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($block in [regex]::Matches($Text, '(?ms)^(?:Import|DelayImport)\s*\{(?<body>.*?)^\}')) {
        $entries = [regex]::Matches($block.Groups['body'].Value, '(?m)^\s*Name:\s*(?<name>[^\r\n]+?)\s*$')
        if ($entries.Count -ne 1) { throw "Invalid import record in $File." }
        $name = $entries[0].Groups['name'].Value
        if ($name.Length -gt 259 -or $name -notmatch '^[A-Za-z0-9_+.-]+\.(dll|drv)$' -or $name.Contains('..')) {
            throw "Invalid imported DLL name in ${File}: $name"
        }
        [void]$names.Add($name)
    }
    return @($names | Sort-Object)
}

function Get-KikiPrepareImports {
    param([string]$ReadObj, [string]$File)
    # Static metadata inspection, not ldd or execution of the supplied EXEs.
    $output = & $ReadObj --file-headers --coff-imports $File 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "PE inspection failed for ${File}: $($output | Select-Object -Last 6 | Out-String)"
    }
    ConvertFrom-KikiPrepareImports ($output -join "`n") $File
}

function Test-KikiPrepareSystemImport {
    param([string]$Name)
    if ($Name -match '^(api-ms-win-|ext-ms-)') { return $true }
    return Test-Path -LiteralPath (Join-Path ([Environment]::SystemDirectory) $Name) -PathType Leaf
}

function New-KikiPrepareCopyItem {
    param([string]$Source, [string]$Destination, [string]$Kind)
    $sourcePath = Get-KikiPreparePlainPath $Source
    $hash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
    $copy = -not (Test-Path -LiteralPath $Destination)
    if (-not $copy) {
        $destinationPath = Get-KikiPreparePlainPath $Destination
        if ((Get-FileHash -LiteralPath $destinationPath -Algorithm SHA256).Hash -ne $hash) {
            throw "Existing file differs from the current source; nothing was overwritten: $Destination"
        }
    }
    [pscustomobject]@{ Source = $sourcePath; Destination = $Destination; Sha256 = $hash; Kind = $Kind; Copy = $copy }
}

function Get-KikiPreparePlan {
    param([string]$Bin, [string]$DependencyBin, [string]$Bios, [string]$ReadObj)
    $Bin = Get-KikiPreparePlainPath $Bin -Directory
    $DependencyBin = Get-KikiPreparePlainPath $DependencyBin -Directory
    $Bios = Get-KikiPreparePlainPath $Bios -Directory
    [void](Get-KikiPreparePlainPath $ReadObj)
    $queue = [Collections.Generic.Queue[object]]::new()
    $seen = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $plan = [Collections.Generic.List[object]]::new()
    foreach ($name in 'qemu-system-aarch64.exe', 'qemu-img.exe', 'qemu-io.exe') {
        $path = Get-KikiPreparePlainPath (Join-Path $Bin $name)
        [void]$seen.Add($name)
        $queue.Enqueue(@{ Source = $path; Name = $name; Kind = 'EXE' })
    }
    while ($queue.Count) {
        $entry = $queue.Dequeue()
        $target = Join-Path $Bin $entry.Name
        $plan.Add((New-KikiPrepareCopyItem $entry.Source $target $entry.Kind))
        foreach ($name in @(Get-KikiPrepareImports $ReadObj $entry.Source)) {
            if ($seen.Contains($name)) { continue }
            $private = Join-Path $Bin $name
            $dependency = Join-Path $DependencyBin $name
            if (Test-Path -LiteralPath $private) {
                # When both exist, require equality; never mix versions silently.
                $source = if (Test-Path -LiteralPath $dependency) { $dependency } else { $private }
            } elseif (Test-KikiPrepareSystemImport $name) {
                continue
            } elseif (Test-Path -LiteralPath $dependency) {
                $source = $dependency
            } else {
                throw "Missing runtime dependency: $name. Keep the build's matching CLANGARM64 packages installed."
            }
            $source = Get-KikiPreparePlainPath $source
            [void]$seen.Add($name)
            $queue.Enqueue(@{ Source = $source; Name = $name; Kind = 'DLL' })
        }
    }
    $roms = Join-Path $Bin 'roms'
    if (Test-Path -LiteralPath $roms) {
        [void](Get-KikiPreparePlainPath $roms -Directory)
        foreach ($file in Get-ChildItem -LiteralPath $roms -Force) {
            if ($file.PSIsContainer -or $file.Extension -notin '.bin', '.rom', '.fd', '.dtb') {
                throw "Unexpected item in private ROM directory: $($file.FullName)"
            }
            [void](Get-KikiPreparePlainPath $file.FullName)
            if (-not (Test-Path -LiteralPath (Join-Path $Bios $file.Name) -PathType Leaf)) {
                throw "Existing ROM is not from this QEMU checkout: $($file.Name)"
            }
        }
    }
    $biosFiles = @(Get-ChildItem -LiteralPath $Bios -File -Force | Where-Object Extension -in '.bin', '.rom', '.fd', '.dtb')
    if (-not $biosFiles.Count) { throw 'No ROMs were found in the matching QEMU pc-bios directory.' }
    foreach ($file in $biosFiles) {
        $plan.Add((New-KikiPrepareCopyItem $file.FullName (Join-Path $roms $file.Name) 'ROM'))
    }
    return $plan.ToArray()
}

function Assert-KikiPrepareNotRunning {
    param([string]$Bin)
    foreach ($process in Get-CimInstance Win32_Process -Filter "Name = 'qemu-system-aarch64.exe' OR Name = 'qemu-img.exe' OR Name = 'qemu-io.exe'") {
        if (-not $process.ExecutablePath) { throw 'Cannot verify a running QEMU process path. Close QEMU before preparing its runtime.' }
        if ([StringComparer]::OrdinalIgnoreCase.Equals([IO.Path]::GetDirectoryName($process.ExecutablePath), $Bin)) {
            throw "This QEMU bin is in use (PID $($process.ProcessId)). Close it before preparing its runtime."
        }
    }
}

function Install-KikiPreparePlan {
    param([object[]]$Plan, [string]$Bin)
    # Preflight the whole closure before creating directories or copying files.
    [void](Get-KikiPreparePlainPath $Bin -Directory)
    Assert-KikiPrepareNotRunning $Bin
    foreach ($item in $Plan) {
        [void](Get-KikiPreparePlainPath $item.Source)
        if ((Get-FileHash -LiteralPath $item.Source -Algorithm SHA256).Hash -ne $item.Sha256) {
            throw "Source changed during inspection: $($item.Source)"
        }
        if (Test-Path -LiteralPath $item.Destination) {
            [void](Get-KikiPreparePlainPath $item.Destination)
            if ((Get-FileHash -LiteralPath $item.Destination -Algorithm SHA256).Hash -ne $item.Sha256) {
                throw "Destination changed during inspection: $($item.Destination)"
            }
        } elseif (-not $item.Copy) {
            throw "Existing file disappeared during inspection: $($item.Destination)"
        }
    }
    $roms = Join-Path $Bin 'roms'
    [void][IO.Directory]::CreateDirectory($roms)
    [void](Get-KikiPreparePlainPath $roms -Directory)
    $copied = 0
    foreach ($item in $Plan) {
        if (-not $item.Copy) { continue }
        [void](Get-KikiPreparePlainPath ([IO.Path]::GetDirectoryName($item.Destination)) -Directory)
        # CREATE_NEW semantics. A concurrently created file causes an error,
        # not an overwrite; leave partial additions in place for inspection.
        [IO.File]::Copy($item.Source, $item.Destination, $false)
        if ((Get-FileHash -LiteralPath $item.Destination -Algorithm SHA256).Hash -ne $item.Sha256) {
            throw "Copied file failed SHA-256 verification: $($item.Destination)"
        }
        $copied++
    }
    return $copied
}

function Invoke-KikiQemuPrepare {
    param([string]$Msys2Path, [string]$BinPath, [string]$SourceDirectory = (Get-Location).Path)
    if (-not $IsWindows) { throw 'This script prepares a Windows ARM64 QEMU runtime.' }
    $source = Get-KikiPreparePlainPath $SourceDirectory -Directory
    if (-not (Test-Path -LiteralPath (Join-Path $source 'configure') -PathType Leaf)) {
        throw 'Run prepare.ps1 from the matching QEMU checkout root.'
    }
    if ([string]::IsNullOrWhiteSpace($BinPath)) { $BinPath = Join-Path $source 'bin' }
    $bin = Get-KikiPreparePlainPath $BinPath -Directory
    $msys = Get-KikiPreparePlainPath $Msys2Path -Directory
    $dependency = Join-Path $msys 'clangarm64/bin'
    $readObj = Join-Path $dependency 'llvm-readobj.exe'
    Assert-KikiPrepareNotRunning $bin
    Write-Host "Inspecting native ARM64 runtime dependencies: $bin"
    $plan = @(Get-KikiPreparePlan $bin $dependency (Join-Path $source 'pc-bios') $readObj)
    $dlls = @($plan | Where-Object Kind -eq 'DLL').Count
    $roms = @($plan | Where-Object Kind -eq 'ROM').Count
    Write-Host "Verified dependency closure: 3 EXEs, $dlls private DLLs, $roms ROMs."
    $copied = Install-KikiPreparePlan $plan $bin
    Write-Host "Runtime prepared: $bin ($copied missing files copied; existing files unchanged)."
    Write-Host 'No compilation, EXE replacement, package installation, QEMU launch or host setting changes were performed.'
    Write-Host 'Static dependencies only: host GPU drivers and a real system boot still require acceptance testing.'
}

# Dot-sourcing is for isolated tests only and never prepares a runtime.
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    try {
        $settings = Resolve-KikiPrepareOptions $Msys2 $BinDirectory $Options
        Invoke-KikiQemuPrepare $settings.Msys2 $settings.Bin
    } catch {
        Write-Error $_
        exit 1
    }
}
