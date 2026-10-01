#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
param([switch]$VerifyRemotePatches)
$ErrorActionPreference = 'Stop'
$project = Split-Path $PSScriptRoot -Parent
. (Join-Path $project 'build.ps1')
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

$settings = Resolve-KikiBuildOptions '' 8 @('--msys2', 'C:\msys2', '--jobs', '4')
Check ($settings.Msys2 -eq 'C:\msys2' -and $settings.Jobs -eq 4) 'double-dash options'
$settings = Resolve-KikiBuildOptions 'D:\msys64' 8 $null
Check ($settings.Msys2 -eq 'D:\msys64' -and $settings.Jobs -eq 8) 'PowerShell parameters and no remaining options'
Reject { Resolve-KikiBuildOptions '' 8 @('--msys2') } 'Missing value' 'missing MSYS2 value'
Reject { Resolve-KikiBuildOptions '' 8 $null } 'Specify your MSYS2' 'missing MSYS2 option'
Reject { Resolve-KikiBuildOptions 'C:\msys64' 8 @('--msys2', 'D:\msys2') } 'only once' 'duplicate MSYS2'
Reject { Resolve-KikiBuildOptions '' 8 @('--unknown', 'value') } 'Unknown build option' 'unknown options'
foreach ($value in @('0', '65', '-1', 'four', '1.5')) {
    Reject { Resolve-KikiBuildOptions 'C:\msys64' 8 @('--jobs', $value) } 'Jobs must' "invalid job count $value"
}
Check (@(Get-KikiMissingPackages @('git', 'make') @('git', 'make')).Count -eq 0) 'complete packages'
Check ((@(Get-KikiMissingPackages @('git', 'make', 'ninja') @('git')) -join ',') -eq 'make,ninja') 'only missing packages'

# Replace only the child-command helper in this scope: do not run pacman or a
# real build in these tests. Verify actual Ensure-KikiBuildPackages decisions.
& {
    $script:packageCommands = [Collections.Generic.List[object]]::new()
    $script:queryCount = 0
    $script:packagesAlreadyPresent = $true
    function Invoke-KikiBuildProcess {
        param($Executable, [string[]]$Arguments, $Environment, [switch]$Capture)
        $script:packageCommands.Add($Arguments)
        if ($Capture) {
            $script:queryCount++
            if ($script:packagesAlreadyPresent -or $script:queryCount -gt 1) { return "git`nmake`nninja" }
            return 'git'
        }
    }
    Ensure-KikiBuildPackages 'fake-bash' @{} @('git', 'make', 'ninja')
    Check ($script:packageCommands.Count -eq 1) 'no installer when all packages exist'
    $script:packageCommands.Clear()
    $script:queryCount = 0
    $script:packagesAlreadyPresent = $false
    Ensure-KikiBuildPackages 'fake-bash' @{} @('git', 'make', 'ninja')
    Check ($script:packageCommands.Count -eq 3) 'query-install-recheck only when missing'
    $install = $script:packageCommands[1]
    Check (($install[-2..-1] -join ',') -eq 'make,ninja') 'install excludes present packages'
    Check ($install[3] -eq 'exec /usr/bin/pacman -S --needed --noconfirm "$@"') 'no whole-install upgrade or partial sync'
}

$environment = Get-KikiMsysEnvironment 'C:\fixture-msys2' 'C:\fixture-qemu'
Check ($environment.MSYSTEM -eq 'CLANGARM64') 'native environment'
Check ($environment.PKG_CONFIG -eq 'C:/fixture-msys2/clangarm64/bin/pkgconf.exe') 'explicit pkg-config'
Check ($environment.PKG_CONFIG_SYSROOT_DIR -eq $null) 'no cross sysroot'
Check ($environment.KIKI_QEMU_BIN -eq 'C:/fixture-qemu/bin') 'direct bin build directory'
Check ($environment.PATH -notmatch 'ucrt64|clang64') 'no x86 toolchain leakage'
Check ($environment.BASH_ENV -eq $null -and $environment.PYTHONPATH -eq $null) 'no shell or Python profile injection'

$recipe = Get-KikiQemuRecipe
Check ($recipe.Patches.Count -eq 4) 'all four patches pinned'
foreach ($patch in $recipe.Patches) {
    Check ((Get-FileHash -LiteralPath (Join-Path $project "patches/$($patch.Name)")).Hash.ToLowerInvariant() -eq $patch.Sha256) "tracked patch hash: $($patch.Name)"
}
Check ((Get-KikiTextHash ($recipe | ConvertTo-Json -Depth 8 -Compress)) -eq 'd0c7ffab1ec9ca8525aac148c2e437dc3cf9916294f33c7a35af8a64202e9247') 'deterministic ordered recipe hash'

# Exercise a real hidden child with pipes, without installing/building/running
# QEMU. PowerShell script-block callbacks on async event threads are avoided.
$pwsh = (Get-Process -Id $PID).Path
$streamed = @(Invoke-KikiBuildProcess $pwsh @('-NoProfile', '-NonInteractive', '-Command',
    '[Console]::Out.WriteLine("stdout-marker"); [Console]::Error.WriteLine("stderr-marker"); if ($null -eq [Console]::In.ReadLine()) { [Console]::Out.WriteLine("stdin-eof") }') 6>&1)
$streamText = ($streamed | ForEach-Object { $_.ToString() }) -join "`n"
Check ($streamText -like '*stdout-marker*') 'streamed child stdout is visible'
Check ($streamText -like '*stderr-marker*') 'streamed child stderr is visible'
Check ($streamText -like '*stdin-eof*') 'hidden child receives valid stdin EOF'
$captured = Invoke-KikiBuildProcess $pwsh @('-NoProfile', '-NonInteractive', '-Command',
    '[Console]::Out.WriteLine("captured-marker")') -Capture
Check ($captured -eq 'captured-marker') 'capture mode still returns only stdout'
Reject { Invoke-KikiBuildProcess $pwsh @('-NoProfile', '-NonInteractive', '-Command',
    '[Console]::Error.WriteLine("specific-child-error"); exit 7') 6>$null } 'specific-child-error' 'streamed failure includes real stderr'
Reject { Invoke-KikiBuildProcess $pwsh @('-NoProfile', '-NonInteractive', '-Command',
    '[Console]::Error.WriteLine("captured-child-error"); exit 9') -Capture } 'captured-child-error' 'captured failure includes real stderr'
$burst = @(Invoke-KikiBuildProcess $pwsh @('-NoProfile', '-NonInteractive', '-Command',
    'for ($i = 0; $i -lt 1600; $i++) { [Console]::Error.WriteLine("stderr-burst-{0:D4}:" + ("x" * 100), $i) }; [Console]::Out.WriteLine("burst-complete")') 6>&1)
Check ((($burst | ForEach-Object { $_.ToString() }) -join "`n") -like '*burst-complete*') 'large stderr pipe drains without deadlock'
$script:liveFirstLine = $false
$script:liveClock = [Diagnostics.Stopwatch]::StartNew()
Invoke-KikiBuildProcess $pwsh @('-NoProfile', '-NonInteractive', '-Command',
    '[Console]::Out.WriteLine("live-before-wait"); [Threading.Thread]::Sleep(1800); [Console]::Out.WriteLine("live-after-wait")') 6>&1 |
    ForEach-Object { if ($_.ToString() -eq 'live-before-wait') { $script:liveFirstLine = $script:liveClock.ElapsedMilliseconds -lt 1600 } }
Check $script:liveFirstLine 'output arrives before child completion'

$git = (Get-Command git.exe -CommandType Application).Source
$scratch = Join-Path ([IO.Path]::GetTempPath()) ('kiki-qemu-script-tests-' + [Guid]::NewGuid().ToString('N'))
[void][IO.Directory]::CreateDirectory($scratch)
try {
    $source = Join-Path $scratch 'qemu'
    [void][IO.Directory]::CreateDirectory($source)
    Invoke-KikiBuildProcess $git @('-C', $source, 'init', '--quiet') -Capture | Out-Null
    Invoke-KikiBuildProcess $git @('-C', $source, 'config', 'core.autocrlf', 'false') -Capture | Out-Null
    [IO.File]::WriteAllText((Join-Path $source 'seed.txt'), "one`n", [Text.UTF8Encoding]::new($false))
    Invoke-KikiBuildProcess $git @('-C', $source, 'add', 'seed.txt') -Capture | Out-Null
    Invoke-KikiBuildProcess $git @('-C', $source, '-c', 'user.name=KikiBuildTests', '-c', 'user.email=tests@example.invalid',
        '-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', 'fixture') -Capture | Out-Null
    $head = Invoke-KikiBuildProcess $git @('-C', $source, 'rev-parse', 'HEAD') -Capture
    $patch1 = "diff --git a/seed.txt b/seed.txt`n--- a/seed.txt`n+++ b/seed.txt`n@@ -1 +1 @@`n-one`n+two`n"
    $patch2 = "diff --git a/seed.txt b/seed.txt`n--- a/seed.txt`n+++ b/seed.txt`n@@ -1 +1 @@`n-two`n+three`n"
    $fixtureRecipe = [ordered]@{
        SourceRevision = $head; PatchRevision = 'fixture'; PatchBaseUrl = 'https://example.invalid'
        Patches = @(
            [ordered]@{ Name = 'one.patch'; Sha256 = (Get-KikiTextHash $patch1) }
            [ordered]@{ Name = 'two.patch'; Sha256 = (Get-KikiTextHash $patch2) }
        ); Packages = @()
    }
    # Existing untracked downloaded entry points are allowed.
    [IO.File]::WriteAllText((Join-Path $source 'build.ps1'), '# fixture download, never executed')
    $receipt = Initialize-KikiPatchState $git $source 'C:\fixture-msys2' $fixtureRecipe
    Check ($receipt.State.Applied -eq 0) 'clean source initializes without modifying it'
    [IO.File]::WriteAllText((Join-Path $receipt.Directory 'one.patch'), $patch1, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $receipt.Directory 'two.patch'), $patch2, [Text.UTF8Encoding]::new($false))
    $files = @(Get-KikiPatchFiles $receipt.Directory $fixtureRecipe)
    Apply-KikiQemuPatches $git $source $receipt @($files[0])
    Check ((Get-Content (Join-Path $source 'seed.txt') -Raw) -eq "two`n") 'first patch applied and checkpointed'
    $receipt = Initialize-KikiPatchState $git $source 'C:\fixture-msys2' $fixtureRecipe
    Apply-KikiQemuPatches $git $source $receipt $files
    Check ((Get-Content (Join-Path $source 'seed.txt') -Raw) -eq "three`n") 'resume patch order'
    $receipt = Initialize-KikiPatchState $git $source 'C:\fixture-msys2' $fixtureRecipe
    Apply-KikiQemuPatches $git $source $receipt $files
    Check ($receipt.State.Applied -eq 2) 'rerun does not reapply patches'
    Reject { Initialize-KikiPatchState $git $source 'D:\different-msys2' $fixtureRecipe } 'changed outside' 'toolchain change refused'
    [IO.File]::WriteAllText((Join-Path $receipt.Directory 'two.patch'), 'corrupt')
    Reject { Get-KikiPatchFiles $receipt.Directory $fixtureRecipe } 'Cached patch hash mismatch' 'corrupt cached patch refused'
    [IO.File]::WriteAllText((Join-Path $source 'seed.txt'), "user edit`n")
    Reject { Initialize-KikiPatchState $git $source 'C:\fixture-msys2' $fixtureRecipe } 'changed outside' 'unrelated tracked edits preserved'
    Check ((Get-Content (Join-Path $source 'seed.txt') -Raw) -eq "user edit`n") 'no reset or overwrite'
    Invoke-KikiBuildProcess $git @('-C', $source, 'add', 'seed.txt') -Capture | Out-Null
    Reject { Initialize-KikiPatchState $git $source 'C:\fixture-msys2' $fixtureRecipe } 'Command failed' 'staged edits refused'

    # A different upstream commit is a warning, not a pin violation. Exercise
    # real patch application after initialization on an unverified revision.
    $unverifiedSource = Join-Path $scratch 'unverified-qemu'
    Invoke-KikiBuildProcess $git @('clone', '--quiet', '--no-hardlinks', $source, $unverifiedSource) -Capture | Out-Null
    $unverifiedRecipe = $fixtureRecipe | ConvertTo-Json -Depth 8 | ConvertFrom-Json -AsHashtable
    $unverifiedRecipe.SourceRevision = '0000000000000000000000000000000000000000'
    $records = @(Initialize-KikiPatchState $git $unverifiedSource 'C:\fixture-msys2' $unverifiedRecipe 3>&1)
    $warnings = @($records | Where-Object { $_ -is [Management.Automation.WarningRecord] })
    $unverifiedReceipt = @($records | Where-Object { $_ -isnot [Management.Automation.WarningRecord] })[0]
    Check ($warnings.Count -eq 1 -and $warnings[0].Message -like '*Continuing with your current checkout*') 'unverified HEAD warns and continues'
    Check ($unverifiedReceipt.State.QemuHead -eq $head) 'receipt records actual unverified HEAD'
    [IO.File]::WriteAllText((Join-Path $unverifiedReceipt.Directory 'one.patch'), $patch1, [Text.UTF8Encoding]::new($false))
    [IO.File]::WriteAllText((Join-Path $unverifiedReceipt.Directory 'two.patch'), $patch2, [Text.UTF8Encoding]::new($false))
    $unverifiedFiles = @(Get-KikiPatchFiles $unverifiedReceipt.Directory $unverifiedRecipe)
    Apply-KikiQemuPatches $git $unverifiedSource $unverifiedReceipt $unverifiedFiles
    Check ((Get-Content (Join-Path $unverifiedSource 'seed.txt')) -eq 'three') 'patches proceed on unverified HEAD'
    $unverifiedReceipt = Initialize-KikiPatchState $git $unverifiedSource 'C:\fixture-msys2' $unverifiedRecipe 3>$null
    Check ($unverifiedReceipt.State.Applied -eq 2) 'unverified HEAD supports incremental rerun'
    Invoke-KikiBuildProcess $git @('-C', $unverifiedSource, 'add', 'seed.txt') -Capture | Out-Null
    Invoke-KikiBuildProcess $git @('-C', $unverifiedSource, '-c', 'user.name=KikiBuildTests', '-c', 'user.email=tests@example.invalid',
        '-c', 'commit.gpgsign=false', 'commit', '--quiet', '-m', 'different fixture HEAD') -Capture | Out-Null
    Reject { Initialize-KikiPatchState $git $unverifiedSource 'C:\fixture-msys2' $unverifiedRecipe 3>$null } 'HEAD changed since' 'stale incremental state refused without forcing recommended commit'
    if ($VerifyRemotePatches) {
        $downloadCache = Join-Path $scratch 'remote-patches'
        [void][IO.Directory]::CreateDirectory($downloadCache)
        $downloaded = @(Get-KikiPatchFiles $downloadCache $recipe)
        Check ($downloaded.Count -eq 4) 'all real immutable remote patch hashes verified'
    }
} finally {
    # Delete only this freshly created unique TEMP fixture, never a user repo.
    $resolved = [IO.Path]::GetFullPath($scratch)
    $tempPrefix = [IO.Path]::GetFullPath([IO.Path]::GetTempPath()).TrimEnd('\', '/') + [IO.Path]::DirectorySeparatorChar
    if (-not $resolved.StartsWith($tempPrefix, [StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($resolved) -notmatch '^kiki-qemu-script-tests-[0-9a-f]{32}$') {
        throw 'Refusing fixture cleanup outside the verified TEMP path.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
Write-Host "PASS: $script:passed build-script checks. No real compilation, pacman installation, VM or public launcher was run."
