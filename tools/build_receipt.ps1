# SPDX-License-Identifier: GPL-2.0-or-later
# Build-only provenance guards. No installation, user registry or VM actions.
function Get-KikiBuildSnapshot {
    param([Parameter(Mandatory)][string]$RepoRoot, [Parameter(Mandatory)][string[]]$SourceFiles)
    $revision = (& git -C $RepoRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $revision -notmatch '^[0-9a-f]{40}$') { throw 'Cannot identify the build source revision.' }
    $tracked = @(& git -C $RepoRoot ls-files)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot identify tracked build inputs.' }
    $clean = -not [bool](& git -C $RepoRoot status --porcelain --untracked-files=no)
    if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect build source state.' }
    $inputs = [ordered]@{}
    foreach ($file in ($SourceFiles | Sort-Object -Unique)) {
        $resolved = (Resolve-Path -LiteralPath $file -ErrorAction Stop).Path
        $relative = [IO.Path]::GetRelativePath($RepoRoot, $resolved).Replace('\', '/')
        if ($relative -eq '..' -or $relative.StartsWith('../') -or [IO.Path]::IsPathRooted($relative)) {
            throw 'Build source inputs must remain inside their recorded source checkout.'
        }
        if ($relative -notin $tracked) { $clean = $false }
        $inputs[$relative] = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    return [ordered]@{ sourceCommit = $revision; sourceTreeClean = $clean; sourceFiles = $inputs }
}
function Initialize-KikiBuildReceipt {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Kind)
    # Invalidate any prior successful receipt BEFORE changing output bytes.
    # This is a generated build file, never a user instance/installation record.
    $record = [ordered]@{ receiptVersion = 1; kind = $Kind; status = 'building-not-distributable' }
    [IO.File]::WriteAllText($Path, ($record | ConvertTo-Json -Depth 4) + "`n", [Text.UTF8Encoding]::new($false))
}
function Complete-KikiBuildReceipt {
    param(
        [Parameter(Mandatory)][string]$RepoRoot,
        [Parameter(Mandatory)]$Snapshot,
        [Parameter(Mandatory)][string]$Compiler,
        [Parameter(Mandatory)][string]$Kind,
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string[]]$OutputFiles,
        [string]$UnitTests = 'not-run'
    )
    $sourcePaths = @($Snapshot.sourceFiles.Keys | ForEach-Object { Join-Path $RepoRoot $_ })
    $after = Get-KikiBuildSnapshot -RepoRoot $RepoRoot -SourceFiles $sourcePaths
    if (($Snapshot | ConvertTo-Json -Depth 6 -Compress) -ne ($after | ConvertTo-Json -Depth 6 -Compress)) {
        throw 'Source changed while compiling. Outputs retain an invalid receipt; rebuild from one source state.'
    }
    $outputs = [ordered]@{}
    foreach ($file in $OutputFiles) {
        $item = Get-Item -LiteralPath $file -ErrorAction Stop
        if ($item.PSIsContainer -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint -or $item.Length -le 0 -or
            $outputs.Contains($item.Name)) { throw 'Invalid/duplicate build output identity.' }
        $outputs[$item.Name] = [ordered]@{ bytes = $item.Length; sha256 = (Get-FileHash -LiteralPath $file).Hash.ToLowerInvariant() }
    }
    $record = [ordered]@{ receiptVersion = 1; kind = $Kind; status = 'compiled'; sourceCommit = $Snapshot.sourceCommit;
        sourceTreeClean = $Snapshot.sourceTreeClean; sourceFiles = $Snapshot.sourceFiles; unitTests = $UnitTests;
        compiler = @{ target = (& $Compiler -dumpmachine).Trim(); sha256 = (Get-FileHash -LiteralPath $Compiler).Hash.ToLowerInvariant();
            version = (@(& $Compiler --version)[0]) }; outputs = $outputs }
    [IO.File]::WriteAllText($Path, ($record | ConvertTo-Json -Depth 7) + "`n", [Text.UTF8Encoding]::new($false))
}
function Assert-KikiBuildReceipt {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Kind,
        [Parameter(Mandatory)][string]$SourceCommit,
        [Parameter(Mandatory)][string]$OutputDirectory,
        [Parameter(Mandatory)][string[]]$OutputNames,
        [switch]$RequireUnitTests
    )
    $item = Get-Item -LiteralPath $Path -ErrorAction Stop
    if ($item.PSIsContainer -or $item.Attributes -band [IO.FileAttributes]::ReparsePoint -or $item.Length -le 0 -or $item.Length -gt 1048576) {
        throw 'Missing, redirected or oversized build receipt.'
    }
    $receipt = [IO.File]::ReadAllText($item.FullName) | ConvertFrom-Json -ErrorAction Stop
    if ($receipt.receiptVersion -ne 1 -or $receipt.kind -cne $Kind -or $receipt.status -cne 'compiled' -or
        $receipt.sourceCommit -cne $SourceCommit -or $receipt.sourceTreeClean -isnot [bool] -or -not $receipt.sourceTreeClean -or
        $receipt.compiler.target -cne 'aarch64-w64-windows-gnu' -or $receipt.compiler.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
        -not $receipt.sourceFiles -or @($receipt.sourceFiles.PSObject.Properties).Count -lt 1 -or
        ($RequireUnitTests -and $receipt.unitTests -cne 'passed')) {
        throw 'Build receipt does not prove a clean, completed native build at the packaging revision.'
    }
    foreach ($name in $OutputNames) {
        if ([IO.Path]::GetFileName($name) -cne $name -or $name -in @('.', '..') -or $name.Contains(':')) { throw 'Invalid receipt output name.' }
        $record = $receipt.outputs.PSObject.Properties[$name]
        $file = Get-Item -LiteralPath (Join-Path $OutputDirectory $name) -ErrorAction Stop
        if (-not $record -or $file.PSIsContainer -or $file.Attributes -band [IO.FileAttributes]::ReparsePoint -or $file.Length -le 0 -or
            $record.Value.bytes -ne $file.Length -or $record.Value.sha256 -cnotmatch '^[0-9a-f]{64}$' -or
            (Get-FileHash -LiteralPath $file.FullName).Hash.ToLowerInvariant() -cne $record.Value.sha256) {
            throw "Build output changed or belongs to a different build: $name"
        }
    }
    return $receipt
}
