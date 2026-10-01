#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path $PSScriptRoot -Parent) 'prepare.ps1')
$script:passed = 0
function Check([bool]$Condition, [string]$Name) {
    if (-not $Condition) { throw "FAILED: $Name" }
    $script:passed++
}
function Reject([scriptblock]$Action, [string]$Message, [string]$Name) {
    try { & $Action } catch {
        Check ($_.Exception.Message -like "*$Message*") $Name
        return
    }
    throw "FAILED: $Name (no rejection)"
}

$settings = Resolve-KikiPrepareOptions '' '' @('--msys2', 'C:\msys64', '--bin', 'D:\bin')
Check ($settings.Msys2 -eq 'C:\msys64' -and $settings.Bin -eq 'D:\bin') 'double-dash options'
$settings = Resolve-KikiPrepareOptions 'C:\msys2' '' $null
Check ($settings.Msys2 -eq 'C:\msys2' -and -not $settings.Bin) 'PowerShell parameter and default bin'
Reject { Resolve-KikiPrepareOptions '' '' $null } 'Specify your MSYS2' 'requires MSYS2'
Reject { Resolve-KikiPrepareOptions '' '' @('--msys2') } 'Missing value' 'missing option value'
Reject { Resolve-KikiPrepareOptions '' '' @('--msys2', '') } 'Missing value' 'empty option value'
Reject { Resolve-KikiPrepareOptions 'C:\msys2' '' @('--msys2', 'C:\msys64') } 'only once' 'duplicate option'
Reject { Resolve-KikiPrepareOptions '' '' @('--unknown', 'value') } 'Unknown prepare option' 'unknown option'

$header = "Format: COFF-ARM64`nArch: aarch64`nImageFileHeader {`n  Machine: IMAGE_FILE_MACHINE_ARM64 (0xAA64)`n}`n"
$text = $header + "Import {`n  Name: SDL2.dll`n  Symbol: Name: not-a-dll`n}`nDelayImport {`n  Name: zlib1.dll`n}`nImport {`n  Name: sdl2.dll`n}`n"
$imports = @(ConvertFrom-KikiPrepareImports $text 'fixture.exe')
Check ($imports.Count -eq 2 -and $imports -contains 'SDL2.dll' -and $imports -contains 'zlib1.dll') 'normal and delay imports; case-insensitive deduplication'
Check (@(ConvertFrom-KikiPrepareImports $header 'fixture.exe').Count -eq 0) 'no imports allowed'
Reject { ConvertFrom-KikiPrepareImports ($header.Replace('IMAGE_FILE_MACHINE_ARM64 (0xAA64)', 'IMAGE_FILE_MACHINE_AMD64 (0x8664)')) 'x86.exe' } 'not native ARM64' 'x86 rejected'
Reject { ConvertFrom-KikiPrepareImports '' 'missing.exe' } 'not native ARM64' 'missing PE header rejected'
foreach ($name in '../bad.dll', '..\bad.dll', 'C:bad.dll', 'a..dll', 'bad.exe', 'bad name.dll', '@response.dll') {
    Reject { ConvertFrom-KikiPrepareImports ($header + "Import {`n  Name: $name`n}`n") 'unsafe.exe' } 'Invalid imported DLL name' "unsafe import: $name"
}
Reject { ConvertFrom-KikiPrepareImports ($header + "Import {`n  Symbol: ignored`n}`n") 'bad.exe' } 'Invalid import record' 'missing DLL name'
Check (Test-KikiPrepareSystemImport 'api-ms-win-crt-runtime-l1-1-0.dll') 'Windows API set not copied'
Check (Test-KikiPrepareSystemImport 'ext-ms-win-example.dll') 'Windows extension API set not copied'
Check (Test-KikiPrepareSystemImport 'KERNEL32.dll') 'system DLL not copied'
Check (-not (Test-KikiPrepareSystemImport 'kiki-unit-test-nonexistent.dll')) 'private DLL not mistaken for system'

# All writes below are new, uniquely named fixtures, never a user QEMU bin.
$fixtureRoot = Join-Path ([IO.Path]::GetTempPath()) ('kiki-prepare-unit-' + [guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($fixtureRoot)
function Write-Fixture([string]$Path, [string]$Text) {
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}
function New-Fixture {
    $root = Join-Path $fixtureRoot ([guid]::NewGuid().ToString('N'))
    $bin = Join-Path $root 'bin'
    $deps = Join-Path $root 'deps'
    $bios = Join-Path $root 'pc-bios'
    foreach ($dir in $bin, $deps, $bios) { [void][IO.Directory]::CreateDirectory($dir) }
    $tool = Join-Path $deps 'llvm-readobj.exe'
    Write-Fixture $tool 'fake tool; never executed'
    foreach ($name in 'qemu-system-aarch64.exe', 'qemu-img.exe', 'qemu-io.exe') {
        Write-Fixture (Join-Path $bin $name) ($header + "Import {`n  Name: a.dll`n}`n")
    }
    Write-Fixture (Join-Path $deps 'a.dll') ($header + "Import {`n  Name: b.dll`n}`nImport {`n  Name: KERNEL32.dll`n}`n")
    Write-Fixture (Join-Path $deps 'b.dll') ($header + "Import {`n  Name: a.dll`n}`n")
    Write-Fixture (Join-Path $deps 'unrelated.dll') $header
    Write-Fixture (Join-Path $bios 'test.bin') 'rom bytes'
    Write-Fixture (Join-Path $bios 'README.txt') 'not a ROM'
    [pscustomobject]@{ Bin = $bin; Deps = $deps; Bios = $bios; Tool = $tool }
}
function Plan-Fixture($Fixture) {
    @(Get-KikiPreparePlan $Fixture.Bin $Fixture.Deps $Fixture.Bios $Fixture.Tool)
}
& {
    # Exercise the real planner/copier with fixture metadata, not actual EXEs.
    function Get-KikiPrepareImports {
        param([string]$ReadObj, [string]$File)
        ConvertFrom-KikiPrepareImports ([IO.File]::ReadAllText($File)) $File
    }
    function Assert-KikiPrepareNotRunning { param([string]$Bin) }
    $f = New-Fixture
    $plan = @(Plan-Fixture $f)
    Check ($plan.Count -eq 6) 'complete closure plus ROM'
    Check (@($plan | Where-Object Kind -eq 'EXE').Count -eq 3) 'all three EXEs checked'
    Check (@($plan | Where-Object { $_.Kind -eq 'EXE' -and $_.Copy }).Count -eq 0) 'EXEs never scheduled for copying'
    Check (@($plan | Where-Object Kind -eq 'DLL').Count -eq 2) 'recursive and cyclic imports resolved once'
    Check (@($plan | Where-Object Copy).Count -eq 3) 'only missing DLLs and ROM'
    Check (-not (Test-Path (Join-Path $f.Bin 'roms'))) 'planning performs no writes'
    $exeHash = (Get-FileHash (Join-Path $f.Bin 'qemu-img.exe')).Hash
    $copied = Install-KikiPreparePlan $plan $f.Bin
    Check ($copied -eq 3) 'copy expected closure'
    Check ((Get-FileHash (Join-Path $f.Bin 'qemu-img.exe')).Hash -eq $exeHash) 'EXE hash preserved'
    Check (-not (Test-Path (Join-Path $f.Bin 'unrelated.dll'))) 'unrelated dependency not copied'
    Check (-not (Test-Path (Join-Path $f.Bin 'KERNEL32.dll'))) 'Windows DLL not copied'
    Check (-not (Test-Path (Join-Path $f.Bin 'roms/README.txt'))) 'ROM documentation not copied'
    $second = @(Plan-Fixture $f)
    Check (@($second | Where-Object Copy).Count -eq 0) 'second preparation is idempotent'
    Check ((Install-KikiPreparePlan $second $f.Bin) -eq 0) 'idempotent no-copy execution'

    Write-Fixture (Join-Path $f.Bin 'a.dll') 'different existing DLL'
    Reject { Plan-Fixture $f } 'Existing file differs' 'conflicting DLL refused without overwrite'
    Check ([IO.File]::ReadAllText((Join-Path $f.Bin 'a.dll')) -eq 'different existing DLL') 'conflicting DLL preserved'

    $f = New-Fixture
    Write-Fixture (Join-Path $f.Deps 'b.dll') ($header + "Import {`n  Name: absent.dll`n}`n")
    Reject { Plan-Fixture $f } 'Missing runtime dependency: absent.dll' 'missing recursive dependency caught'
    Check (-not (Test-Path (Join-Path $f.Bin 'a.dll')) -and -not (Test-Path (Join-Path $f.Bin 'roms'))) 'missing dependency leaves bin untouched'

    $f = New-Fixture
    Write-Fixture (Join-Path $f.Deps 'b.dll') 'AMD64 dependency'
    Reject { Plan-Fixture $f } 'not native ARM64' 'wrong dependency architecture refused'
    Check (-not (Test-Path (Join-Path $f.Bin 'a.dll'))) 'architecture preflight copies nothing'

    $f = New-Fixture
    $plan = @(Plan-Fixture $f)
    Write-Fixture (Join-Path $f.Deps 'b.dll') 'changed after plan'
    Reject { Install-KikiPreparePlan $plan $f.Bin } 'Source changed' 'source mutation after inspection caught'
    Check (-not (Test-Path (Join-Path $f.Bin 'a.dll')) -and -not (Test-Path (Join-Path $f.Bin 'roms'))) 'source mutation detected before any copy'

    $f = New-Fixture
    $plan = @(Plan-Fixture $f)
    Write-Fixture (Join-Path $f.Bin 'b.dll') 'concurrent different destination'
    Reject { Install-KikiPreparePlan $plan $f.Bin } 'Destination changed' 'target mutation caught'
    Check ([IO.File]::ReadAllText((Join-Path $f.Bin 'b.dll')) -eq 'concurrent different destination') 'concurrent target not overwritten'
    Check (-not (Test-Path (Join-Path $f.Bin 'a.dll'))) 'target mutation detected before first copy'

    $f = New-Fixture
    [void][IO.Directory]::CreateDirectory((Join-Path $f.Bin 'roms'))
    Write-Fixture (Join-Path $f.Bin 'roms/test.bin') 'conflicting ROM'
    Reject { Plan-Fixture $f } 'Existing file differs' 'conflicting ROM rejected'
    Write-Fixture (Join-Path $f.Bin 'roms/extra.txt') 'not allowed'
    Reject { Plan-Fixture $f } 'Unexpected item' 'invalid ROM item rejected'

    $f = New-Fixture
    [void][IO.Directory]::CreateDirectory((Join-Path $f.Bin 'roms'))
    Write-Fixture (Join-Path $f.Bin 'roms/foreign.bin') 'old ROM'
    Reject { Plan-Fixture $f } 'not from this QEMU checkout' 'ROM from another source rejected'
}
& {
    function Get-CimInstance {
        param($ClassName, $Filter)
        @([pscustomobject]@{ ExecutablePath = 'C:\fixture\bin\qemu-system-aarch64.exe'; ProcessId = 123 })
    }
    Reject { Assert-KikiPrepareNotRunning 'C:\fixture\bin' } 'in use' 'running matching bin refused'
    Assert-KikiPrepareNotRunning 'D:\different\bin'
    Check $true 'other QEMU bins not blocked'
}
& {
    function Get-CimInstance { param($ClassName, $Filter); @([pscustomobject]@{ ExecutablePath = $null; ProcessId = 123 }) }
    Reject { Assert-KikiPrepareNotRunning 'C:\fixture\bin' } 'Cannot verify' 'unknown process identity refused'
}
Write-Host "$script:passed checks passed. Only isolated generated fixtures were written: $fixtureRoot"
