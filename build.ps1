#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
<#
.SYNOPSIS
Build QEMU with KikiEmu's fixed patches in a user's upstream QEMU checkout.
.EXAMPLE
./build.ps1 --msys2 'C:\msys64'
.EXAMPLE
./build.ps1 -Msys2 'D:\msys2' -Jobs 4
.NOTES
Run from the QEMU checkout root. Only dependencies, patches and compilation
are handled here: bin is the build directory, not a private runtime export.
No KikiEmu checkout, installer, internal inspector or WSL is required.
#>
[CmdletBinding(PositionalBinding = $false)]
param(
    [string]$Msys2,
    [ValidateRange(1, 64)][int]$Jobs = 8,
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$Options
)

function Resolve-KikiBuildOptions {
    param([string]$Msys2Path, [int]$JobCount, [string[]]$ExtraOptions)
    $hasMsys2 = -not [string]::IsNullOrWhiteSpace($Msys2Path)
    $hasJobs = $false
    for ($i = 0; $i -lt $ExtraOptions.Count; $i++) {
        $option = $ExtraOptions[$i]
        if ($option -notin '--msys2', '--jobs') { throw "Unknown build option: $option" }
        if ($i + 1 -ge $ExtraOptions.Count) { throw "Missing value for $option." }
        $value = $ExtraOptions[++$i]
        if ($option -eq '--msys2') {
            if ($hasMsys2) { throw 'Specify the MSYS2 path only once.' }
            $Msys2Path = $value
            $hasMsys2 = $true
        } else {
            if ($hasJobs) { throw 'Specify --jobs only once.' }
            $parsedJobs = 0
            if (-not [int]::TryParse($value, [ref]$parsedJobs) -or $parsedJobs -lt 1 -or $parsedJobs -gt 64) {
                throw 'Jobs must be an integer from 1 to 64.'
            }
            $JobCount = $parsedJobs
            $hasJobs = $true
        }
    }
    if ([string]::IsNullOrWhiteSpace($Msys2Path)) {
        throw "Specify your MSYS2 installation: ./build.ps1 --msys2 'C:\msys64'"
    }
    [pscustomobject]@{ Msys2 = $Msys2Path; Jobs = $JobCount }
}

function Get-KikiQemuRecipe {
    [ordered]@{
        SourceRevision = 'bde658eef6b38c45794bfd7ad4d2dd1b574e4694'
        PatchRevision = '2130e523573d3304a3fa05b3c88e5dca79ff3ff5'
        PatchBaseUrl = 'https://raw.githubusercontent.com/kekeqwq/KikiEmu'
        Patches = @(
            [ordered]@{ Name = 'qemu-kikiaosp-tested-surface-20260929.patch'; Sha256 = 'afce6303ea027eec662db29bc4039b67947b7fcead0c49986f69ea1de1c69e0b' }
            [ordered]@{ Name = 'qemu-io-binary-source.patch'; Sha256 = 'c99af88556a20ca7ba5da102fdd7b5a5b08ea41a26992dcc61fc5f72b0252357' }
            [ordered]@{ Name = 'qemu-sdl-boot-console.patch'; Sha256 = '681600b5b06baeb823242e1bb4c3a3358ee50db2dc84dca5392b915049420d3b' }
            [ordered]@{ Name = 'qemu-sdl-channel-title.patch'; Sha256 = 'ad41ebace69e5c6132312c9b41fc1710846ccd7ec4f0478badf53924a8b93239' }
            [ordered]@{ Name = 'qemu-sdl-managed-close.patch'; Sha256 = '30e9949220f1b9dce41bd61af38065c847ef912bf6cb86eaa0a9b5216299fa43'; Revision = '710172b1a4ee6cbd89eb65f16b762a696e896f53' }
        )
        Packages = @(
            'git', 'make', 'ninja',
            'mingw-w64-clang-aarch64-clang',
            'mingw-w64-clang-aarch64-python',
            'mingw-w64-clang-aarch64-python-packaging',
            'mingw-w64-clang-aarch64-pkgconf',
            'mingw-w64-clang-aarch64-glib2',
            'mingw-w64-clang-aarch64-gtk3',
            'mingw-w64-clang-aarch64-SDL2',
            'mingw-w64-clang-aarch64-libslirp',
            'mingw-w64-clang-aarch64-pixman',
            'mingw-w64-clang-aarch64-zstd',
            'mingw-w64-clang-aarch64-libepoxy',
            'mingw-w64-clang-aarch64-virglrenderer'
        )
    }
}

function Invoke-KikiBuildProcess {
    param([string]$Executable, [string[]]$Arguments, [hashtable]$Environment = @{}, [switch]$Capture)
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Executable
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    foreach ($argument in $Arguments) { $start.ArgumentList.Add($argument) }
    foreach ($name in $Environment.Keys) {
        if ($null -eq $Environment[$name]) { [void]$start.Environment.Remove($name) }
        else { $start.Environment[$name] = $Environment[$name] }
    }
    # A hidden Windows child must not rely on inherited console handles.
    # Always supply real pipes, then either capture or forward their output.
    $start.RedirectStandardInput = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.UTF8Encoding]::new($false)
    $start.StandardErrorEncoding = [Text.UTF8Encoding]::new($false)
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $start
    try {
        [void]$process.Start()
        $process.StandardInput.Close() # Explicit EOF; every build command is noninteractive.
        if ($Capture) {
            $stdout = $process.StandardOutput.ReadToEndAsync()
            $stderr = $process.StandardError.ReadToEndAsync()
        } else {
            $stdoutLine = $process.StandardOutput.ReadLineAsync()
            $stderrLine = $process.StandardError.ReadLineAsync()
            $tail = [Collections.Generic.Queue[string]]::new()
            # Drain both pipes concurrently so a full stderr/stdout pipe never
            # deadlocks Ninja or Python. Forward lines while the child runs,
            # not only after a potentially hours-long compilation completes.
            while ($null -ne $stdoutLine -or $null -ne $stderrLine) {
                $pending = [Threading.Tasks.Task[]]@(@($stdoutLine, $stderrLine) | Where-Object { $null -ne $_ })
                [void][Threading.Tasks.Task]::WaitAny($pending, 100)
                if ($null -ne $stdoutLine -and $stdoutLine.IsCompleted) {
                    $line = $stdoutLine.GetAwaiter().GetResult()
                    if ($null -eq $line) { $stdoutLine = $null }
                    else {
                        Write-Host $line
                        $tail.Enqueue($line)
                        $stdoutLine = $process.StandardOutput.ReadLineAsync()
                    }
                }
                if ($null -ne $stderrLine -and $stderrLine.IsCompleted) {
                    $line = $stderrLine.GetAwaiter().GetResult()
                    if ($null -eq $line) { $stderrLine = $null }
                    else {
                        Write-Host $line
                        $tail.Enqueue($line)
                        $stderrLine = $process.StandardError.ReadLineAsync()
                    }
                }
                while ($tail.Count -gt 80) { [void]$tail.Dequeue() }
            }
        }
        $process.WaitForExit()
        if ($Capture) {
            $output = $stdout.GetAwaiter().GetResult()
            $errorText = $stderr.GetAwaiter().GetResult()
        }
        if ($process.ExitCode -ne 0) {
            if ($Capture) { throw "Command failed ($($process.ExitCode)): $Executable`n$errorText$output" }
            throw "Command failed ($($process.ExitCode)): $Executable`nLast build output:`n$($tail -join "`n")"
        }
        if ($Capture) { return $output.TrimEnd([char[]]"`r`n") }
    } finally { $process.Dispose() }
}

function Get-KikiMsysEnvironment {
    param([string]$Root, [string]$Source)
    $prefix = Join-Path $Root 'clangarm64'
    $environment = @{
        MSYSTEM = 'CLANGARM64'; MINGW_PREFIX = '/clangarm64'; MSYSTEM_PREFIX = '/clangarm64'
        MSYSTEM_CARCH = 'aarch64'; MINGW_CHOST = 'aarch64-w64-mingw32'
        MINGW_PACKAGE_PREFIX = 'mingw-w64-clang-aarch64'
        PATH = "$prefix\bin;$Root\usr\bin;$env:SystemRoot\System32;$env:SystemRoot"
        PKG_CONFIG = (Join-Path $prefix 'bin/pkgconf.exe').Replace('\', '/')
        PKG_CONFIG_LIBDIR = (Join-Path $prefix 'lib/pkgconfig').Replace('\', '/')
        PKG_CONFIG_PATH = (Join-Path $prefix 'lib/pkgconfig').Replace('\', '/')
        KIKI_QEMU_SOURCE = $Source.Replace('\', '/')
        KIKI_QEMU_BIN = (Join-Path $Source 'bin').Replace('\', '/')
    }
    # Never edit the host's persistent PATH, registry, or MSYS2 profiles.
    foreach ($name in @('BASH_ENV', 'ENV', 'CDPATH', 'PKG_CONFIG_SYSROOT_DIR', 'CC', 'CXX',
            'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS', 'CPATH', 'C_INCLUDE_PATH',
            'CPLUS_INCLUDE_PATH', 'LIBRARY_PATH', 'PYTHONHOME', 'PYTHONPATH',
            'MSYS2_ARG_CONV_EXCL', 'MSYS2_ENV_CONV_EXCL')) { $environment[$name] = $null }
    $environment
}

function Get-KikiMissingPackages {
    param([string[]]$Required, [string[]]$Installed)
    $known = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
    foreach ($package in $Installed) { [void]$known.Add($package.Trim()) }
    foreach ($package in $Required) { if (-not $known.Contains($package)) { $package } }
}

function Ensure-KikiBuildPackages {
    param([string]$Bash, [hashtable]$Environment, [string[]]$Required)
    $installed = Invoke-KikiBuildProcess $Bash @('--noprofile', '--norc', '-c', 'exec /usr/bin/pacman -Qq') $Environment -Capture
    $missing = @(Get-KikiMissingPackages $Required ($installed -split '\r?\n'))
    if ($missing.Count -eq 0) {
        Write-Host 'All required packages are installed; skipping installation.'
        return
    }
    Write-Host "Installing missing packages only: $($missing -join ', ')"
    # Do not silently upgrade the installation or refresh it with a partial -Sy.
    Invoke-KikiBuildProcess $Bash (@('--noprofile', '--norc', '-c',
        'exec /usr/bin/pacman -S --needed --noconfirm "$@"', 'kiki-qemu') + $missing) $Environment
    $installed = Invoke-KikiBuildProcess $Bash @('--noprofile', '--norc', '-c', 'exec /usr/bin/pacman -Qq') $Environment -Capture
    if (@(Get-KikiMissingPackages $Required ($installed -split '\r?\n')).Count -ne 0) {
        throw 'Required packages are still missing. Update MSYS2 in its own terminal, then retry.'
    }
}

function Get-KikiTextHash {
    param([string]$Text)
    [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes($Text))).ToLowerInvariant()
}

function Get-KikiSourceDiffHash {
    param([string]$Git, [string]$Source)
    $diff = Invoke-KikiBuildProcess $Git @('-C', $Source, 'diff', '--no-ext-diff', '--no-textconv', '--binary', 'HEAD', '--') -Capture
    Get-KikiTextHash $diff
}

function Save-KikiPatchState {
    param([string]$Path, [System.Collections.IDictionary]$State)
    # This is builder metadata, never a QEMU executable or runtime export.
    [IO.File]::WriteAllText("$Path.tmp", ($State | ConvertTo-Json -Depth 8) + "`n", [Text.UTF8Encoding]::new($false))
    [IO.File]::Move("$Path.tmp", $Path, $true)
}

function Initialize-KikiPatchState {
    param([string]$Git, [string]$Source, [string]$MsysRoot, [System.Collections.IDictionary]$Recipe)
    $head = Invoke-KikiBuildProcess $Git @('-C', $Source, 'rev-parse', 'HEAD') -Capture
    if ($head -ne $Recipe.SourceRevision) {
        Write-Warning "QEMU revision $head has not been verified with these patches. Patching or compilation may fail. Recommended revision: $($Recipe.SourceRevision). Continuing with your current checkout."
    }
    # Staged user changes must not be mistaken for this builder's patches.
    Invoke-KikiBuildProcess $Git @('-C', $Source, 'diff', '--cached', '--quiet', '--') -Capture | Out-Null
    $cache = Join-Path $Source '.kiki-qemu-build'
    $statePath = Join-Path $cache 'state.json'
    $diffHash = Get-KikiSourceDiffHash $Git $Source
    $recipeHash = Get-KikiTextHash ($Recipe | ConvertTo-Json -Depth 8 -Compress)
    if (Test-Path -LiteralPath $cache) {
        Assert-KikiPlainDirectory $cache
        if (-not (Test-Path -LiteralPath $statePath -PathType Leaf)) { throw "Unmanaged build cache: $cache. Use a new clean checkout." }
        $state = Get-Content -LiteralPath $statePath -Raw | ConvertFrom-Json -AsHashtable
        # Record the user's actual revision, not an enforced recommended pin.
        # Old receipts came from the former pinned builder, so their HEAD is
        # known. Do not reuse already patched build state with another HEAD.
        $previousHead = if ($state.Contains('QemuHead')) { $state.QemuHead } else { $Recipe.SourceRevision }
        if ($previousHead -ne $head) {
            throw 'QEMU HEAD changed since this incremental build was prepared. Use a new checkout for that revision; no source or artifacts were reset.'
        }
        $upgrade = Test-KikiManagedCloseUpgrade $Recipe $state.RecipeHash $state.Applied
        if ($state.Version -ne 1 -or $state.Source -ne $Source -or $state.Msys2 -ne $MsysRoot -or
            ($state.RecipeHash -ne $recipeHash -and -not $upgrade) -or $state.DiffHash -ne $diffHash -or
            ($state.Applied -isnot [long] -and $state.Applied -isnot [int]) -or
            $state.Applied -lt 0 -or $state.Applied -gt $Recipe.Patches.Count) {
            throw 'Source, build recipe or MSYS2 path changed outside this build. No files were reset; use a new clean checkout.'
        }
        if ($upgrade) {
            $state.RecipeHash = $recipeHash
            Save-KikiPatchState $statePath $state
            Write-Host 'Verified four-patch build: adding the managed-close fix incrementally; existing artifacts retained.'
        }
    } else {
        if ($diffHash -ne (Get-KikiTextHash '')) { throw 'QEMU has local tracked changes. No patches were applied; use a clean checkout.' }
        $bin = Join-Path $Source 'bin'
        if (Test-Path -LiteralPath $bin) {
            Assert-KikiPlainDirectory $bin
            if (@(Get-ChildItem -LiteralPath $bin -Force).Count -ne 0) { throw 'An unmanaged bin directory already exists. No build files were overwritten.' }
        }
        $state = [ordered]@{ Version = 1; Source = $Source; Msys2 = $MsysRoot; RecipeHash = $recipeHash; QemuHead = $head; Applied = 0; DiffHash = $diffHash }
        [void][IO.Directory]::CreateDirectory($cache)
        Save-KikiPatchState $statePath $state
    }
    [pscustomobject]@{ Directory = $cache; Path = $statePath; State = $state; QemuHead = $head }
}

function Assert-KikiPlainDirectory {
    param([string]$Path)
    $item = Get-Item -LiteralPath $Path -Force
    if (-not $item.PSIsContainer -or ($item.Attributes -band [IO.FileAttributes]::ReparsePoint)) {
        throw "Expected an ordinary directory, not a linked/reparse path: $Path"
    }
}

function Test-KikiManagedCloseUpgrade {
    param([System.Collections.IDictionary]$Recipe, [string]$PreviousHash, $Applied)
    if ($PreviousHash -ne 'd0c7ffab1ec9ca8525aac148c2e437dc3cf9916294f33c7a35af8a64202e9247' -or
        ($Applied -isnot [long] -and $Applied -isnot [int]) -or $Applied -ne 4 -or
        $Recipe.Patches.Count -ne 5 -or $Recipe.Patches[4].Name -ne 'qemu-sdl-managed-close.patch') { return $false }
    $legacy = [ordered]@{}
    foreach ($key in $Recipe.Keys) {
        $legacy[$key] = if ($key -eq 'Patches') { @($Recipe.Patches[0..3]) } else { $Recipe[$key] }
    }
    return (Get-KikiTextHash ($legacy | ConvertTo-Json -Depth 8 -Compress)) -eq $PreviousHash
}

function Get-KikiPatchFiles {
    param([string]$Cache, [System.Collections.IDictionary]$Recipe)
    foreach ($patch in $Recipe.Patches) {
        $path = Join-Path $Cache $patch.Name
        if (-not (Test-Path -LiteralPath $path)) {
            $revision = if ($patch.Contains('Revision')) { $patch.Revision } else { $Recipe.PatchRevision }
            $url = "$($Recipe.PatchBaseUrl)/$revision/patches/$($patch.Name)"
            Write-Host "Downloading pinned patch: $($patch.Name)"
            Invoke-WebRequest -Uri $url -OutFile "$path.download"
            if ((Get-FileHash -LiteralPath "$path.download" -Algorithm SHA256).Hash.ToLowerInvariant() -ne $patch.Sha256) {
                throw "Downloaded patch hash mismatch: $($patch.Name). No unverified patch will be applied."
            }
            [IO.File]::Move("$path.download", $path)
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $patch.Sha256) {
            throw "Cached patch hash mismatch: $($patch.Name). No unverified patch will be applied."
        }
        $path
    }
}

function Apply-KikiQemuPatches {
    param([string]$Git, [string]$Source, $Receipt, [string[]]$PatchFiles)
    if ($Receipt.State.Applied -eq $PatchFiles.Count) { Write-Host 'Pinned patches already applied; preserving the incremental build.' }
    for ($i = [int]$Receipt.State.Applied; $i -lt $PatchFiles.Count; $i++) {
        Write-Host "Applying patch $($i + 1)/$($PatchFiles.Count): $([IO.Path]::GetFileName($PatchFiles[$i]))"
        Invoke-KikiBuildProcess $Git @('-C', $Source, 'apply', '--check', '--', $PatchFiles[$i]) -Capture | Out-Null
        Invoke-KikiBuildProcess $Git @('-C', $Source, 'apply', '--', $PatchFiles[$i]) -Capture | Out-Null
        $Receipt.State.Applied = $i + 1
        $Receipt.State.DiffHash = Get-KikiSourceDiffHash $Git $Source
        Save-KikiPatchState $Receipt.Path $Receipt.State
    }
}

function Invoke-KikiQemuBuild {
    param([string]$Msys2Path, [int]$JobCount)
    if (-not $IsWindows -or [Runtime.InteropServices.RuntimeInformation]::OSArchitecture -ne 'Arm64') {
        throw 'This recipe requires native Windows ARM64 and MSYS2 CLANGARM64.'
    }
    $source = (Get-Location).ProviderPath
    Assert-KikiPlainDirectory $source
    foreach ($file in @('configure', 'VERSION', 'meson.build')) {
        if (-not (Test-Path -LiteralPath (Join-Path $source $file) -PathType Leaf)) { throw 'Run build.ps1 from the upstream QEMU checkout root.' }
    }
    $root = (Resolve-Path -LiteralPath $Msys2Path).ProviderPath
    Assert-KikiPlainDirectory $root
    # Upstream configure/Makefile do not support whitespace in source paths.
    if ($source -match '\s' -or $root -match '\s') { throw 'QEMU configure requires source and tool paths without whitespace. Choose paths without spaces.' }
    $bash = Join-Path $root 'usr/bin/bash.exe'
    foreach ($file in @('usr/bin/bash.exe', 'usr/bin/pacman.exe', 'usr/bin/cygpath.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $root $file) -PathType Leaf)) { throw "Not a complete MSYS2 installation: $root (missing $file)." }
    }
    $gitCommand = Get-Command git.exe -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    $git = if ($gitCommand) { $gitCommand.Source } else { Join-Path $root 'usr/bin/git.exe' }
    $top = Invoke-KikiBuildProcess $git @('-C', $source, 'rev-parse', '--show-toplevel') -Capture
    if ([IO.Path]::GetFullPath($top) -ne $source) { throw 'Run from the QEMU checkout root, not a subdirectory.' }
    $recipe = Get-KikiQemuRecipe
    $receipt = Initialize-KikiPatchState $git $source $root $recipe
    $environment = Get-KikiMsysEnvironment $root $source
    Write-Host "QEMU revision: $($receipt.QemuHead)"
    Write-Host "MSYS2: $root (CLANGARM64); build directory: $source\bin; jobs: $JobCount"
    Ensure-KikiBuildPackages $bash $environment $recipe.Packages
    $patchFiles = @(Get-KikiPatchFiles $receipt.Directory $recipe)
    Apply-KikiQemuPatches $git $source $receipt $patchFiles
    $bin = Join-Path $source 'bin'
    [void][IO.Directory]::CreateDirectory($bin)
    Assert-KikiPlainDirectory $bin
    $configure = @'
set -euo pipefail
cd "$(cygpath -u "$KIKI_QEMU_BIN")"
if [[ ! -f build.ninja ]]; then
    source_dir=$(cygpath -u "$KIKI_QEMU_SOURCE")
    /usr/bin/bash "$source_dir/configure" \
        --cpu=aarch64 --target-list=aarch64-softmmu \
        --cc=clang --cxx=clang++ --python=/clangarm64/bin/python.exe \
        --enable-whpx --enable-gtk --enable-sdl --enable-opengl \
        --enable-virglrenderer --enable-slirp \
        --disable-werror --disable-docs --disable-dbus-display
fi
exec /usr/bin/ninja -j "$1" qemu-system-aarch64.exe qemu-img.exe qemu-io.exe
'@
    Invoke-KikiBuildProcess $bash @('--noprofile', '--norc', '-c', $configure.Replace("`r`n", "`n"), 'kiki-qemu', "$JobCount") $environment
    foreach ($name in @('qemu-system-aarch64.exe', 'qemu-img.exe', 'qemu-io.exe')) {
        if (-not (Test-Path -LiteralPath (Join-Path $bin $name) -PathType Leaf)) { throw "Build did not produce $name." }
    }
    Write-Host "Build complete: $bin"
    Write-Host 'Build-only: artifacts remain untouched. No DLL/ROM export, installation, cleanup or QEMU launch was performed.'
}

# Dot-sourcing loads helpers for isolated tests; it never starts a build.
if ($MyInvocation.InvocationName -ne '.') {
    $ErrorActionPreference = 'Stop'
    try {
        $settings = Resolve-KikiBuildOptions $Msys2 $Jobs $Options
        Invoke-KikiQemuBuild $settings.Msys2 $settings.Jobs
    } catch {
        Write-Error $_
        exit 1
    }
}
