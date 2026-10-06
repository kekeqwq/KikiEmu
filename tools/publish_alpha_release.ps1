#Requires -Version 7.0
# SPDX-License-Identifier: GPL-2.0-or-later
param(
    [Parameter(Mandatory)][string]$Plan,
    [Parameter(Mandatory)][string]$JournalDirectory,
    [ValidateSet('Validate','Draft','Publish')][string]$Mode='Validate'
)
# Explicit, user-authorized two-repository release. No asset replacement,
# published-release mutation, installer execution or credentials on disk.
$ErrorActionPreference='Stop'
$planPath=(Resolve-Path -LiteralPath $Plan).Path
$planHash=(Get-FileHash $planPath).Hash.ToLowerInvariant()
$spec=Get-Content -LiteralPath $planPath -Raw | ConvertFrom-Json
if ($spec.version -notin @('0.1.0-alpha','0.2.0-alpha','0.3.0-alpha','0.3.1-alpha') -or $spec.releases.Count -ne 2) { throw 'Expected an explicit supported two-repository Alpha plan.' }
$expectedTag='v'+$spec.version
$seen=@{}
foreach ($release in $spec.releases) {
    if ($release.repository -notin @('kekeqwq/KikiEmu','kekeqwq/kikiaosp_test') -or $seen.ContainsKey($release.repository) -or
        $release.tag -ne $expectedTag -or $release.commit -notmatch '^[0-9a-f]{40}$') { throw 'Unexpected repository/tag/revision.' }
    $seen[$release.repository]=$true
    $names=@{}
    $required=if ($release.repository -eq 'kekeqwq/KikiEmu') {'setup.exe'} elseif ($spec.version -in @('0.3.0-alpha','0.3.1-alpha')) {'KikiAOSP-'+$spec.version+'-arm64-ab.zip'} else {'KikiAOSP-'+$spec.version+'-arm64.zip'}
    if ($release.assets.Count -lt 3 -or -not ($release.assets.path | Where-Object { [IO.Path]::GetFileName($_) -eq $required })) {
        throw 'Each release requires its primary download plus source/provenance assets.'
    }
    if (-not (Test-Path -LiteralPath $release.notes -PathType Leaf)) { throw 'Release notes are missing.' }
    foreach ($asset in $release.assets) {
        $file=Get-Item -LiteralPath $asset.path
        if ($file.PSIsContainer -or $file.Length -le 0 -or $file.Length -ge 2GB -or
            $file.Name -notmatch '^[A-Za-z0-9_.-]+$' -or $names.ContainsKey($file.Name) -or
            $file.Length -ne $asset.bytes -or (Get-FileHash $file.FullName).Hash.ToLowerInvariant() -ne $asset.sha256) {
            throw "Asset validation failed: $($asset.path)"
        }
        $names[$file.Name]=$true
    }
}
Write-Host 'Both repositories and all local release asset hashes validated.'
if ($Mode -eq 'Validate') { return }
$journal=[IO.Path]::GetFullPath($JournalDirectory)
$journalFile=Join-Path $journal 'publication.json'
if (Test-Path -LiteralPath $journal) {
    if (-not (Test-Path -LiteralPath $journalFile)) { throw 'Existing directory is not this publication journal.' }
    $previous=Get-Content -LiteralPath $journalFile -Raw | ConvertFrom-Json
    if ($previous.planSha256 -ne $planHash) { throw 'Publication plan changed; no assets may be silently replaced.' }
} else {
    if ($Mode -eq 'Publish') { throw 'Create and verify BOTH draft releases first.' }
    [void][IO.Directory]::CreateDirectory($journal)
    [IO.File]::WriteAllText($journalFile,(@{planSha256=$planHash;status='draft-preparation'}|ConvertTo-Json))
}
$env:GIT_TERMINAL_PROMPT='0'; $env:GCM_INTERACTIVE='never'
$raw="protocol=https`nhost=github.com`n`n" | git -c credential.interactive=never credential fill 2>$null
if ($LASTEXITCODE -ne 0) { throw 'Existing GitHub credential could not be obtained non-interactively.' }
$credential=@{}
foreach ($line in $raw) { $parts=$line -split '=',2; if ($parts.Count -eq 2) { $credential[$parts[0]]=$parts[1] } }
if (-not $credential.password) { throw 'No GitHub credential available.' }
$headers=@{Authorization='Bearer '+$credential.password;Accept='application/vnd.github+json';
    'X-GitHub-Api-Version'='2022-11-28';'User-Agent'='KikiEmu-alpha-publisher'}
function Api([string]$Path,[string]$Method='Get',$Body=$null) {
    $options=@{Uri='https://api.github.com/'+$Path;Headers=$headers;Method=$Method;TimeoutSec=90}
    if ($null -ne $Body) { $options.Body=$Body|ConvertTo-Json -Depth 15; $options.ContentType='application/json' }
    Invoke-RestMethod @options
}
function Find-Release([string]$Repository) {
    $releases=Api "repos/$Repository/releases?per_page=100"
    $found=@($releases | Where-Object tag_name -eq $expectedTag)
    if ($found.Count -gt 1) { throw 'Duplicate release identity.' }
    if ($found.Count -eq 1) { return $found[0] }
    return $null
}
function Verify-RemoteAssets($Definition,$Remote) {
    $assets=Api "repos/$($Definition.repository)/releases/$($Remote.id)/assets?per_page=100"
    if ($assets.Count -ne $Definition.assets.Count) { throw 'Unexpected/missing remote assets.' }
    $directory=Join-Path $journal ($Definition.repository -replace '/','--')
    [void][IO.Directory]::CreateDirectory($directory)
    foreach ($expected in $Definition.assets) {
        $name=[IO.Path]::GetFileName($expected.path)
        $actual=@($assets | Where-Object name -eq $name)
        if ($actual.Count -ne 1 -or $actual[0].state -ne 'uploaded' -or $actual[0].size -ne $expected.bytes) { throw "Remote asset incomplete: $name" }
        if ($actual[0].digest -and $actual[0].digest -ne ('sha256:'+$expected.sha256)) { throw "GitHub asset digest mismatch: $name" }
        $download=Join-Path $directory $name
        if (-not (Test-Path -LiteralPath $download)) {
            Write-Host "Downloading uploaded bytes for SHA-256 verification: $($Definition.repository)/$name"
            $downloadHeaders=$headers.Clone(); $downloadHeaders.Accept='application/octet-stream'
            Invoke-WebRequest -Uri "https://api.github.com/repos/$($Definition.repository)/releases/assets/$($actual[0].id)" -Headers $downloadHeaders -OutFile $download -TimeoutSec 3600
        }
        if ((Get-Item $download).Length -ne $expected.bytes -or (Get-FileHash $download).Hash.ToLowerInvariant() -ne $expected.sha256) {
            throw "Uploaded download SHA-256 mismatch: $name"
        }
        Write-Host "Verified uploaded asset: $name"
    }
}
try {
    $remoteReleases=@{}
    foreach ($definition in $spec.releases) {
        $repo=$definition.repository
        $tag=Api "repos/$repo/git/ref/tags/$($definition.tag)"
        $object=$tag.object
        if ($object.type -eq 'tag') { $object=(Api "repos/$repo/git/tags/$($object.sha)").object }
        if ($object.type -ne 'commit' -or $object.sha -ne $definition.commit) { throw 'Remote immutable tag does not match the release source commit.' }
        $body=Get-Content -LiteralPath $definition.notes -Raw
        $remote=Find-Release $repo
        if ($remote) {
            $resumePublished=$Mode -eq 'Publish' -and $previous.planSha256 -eq $planHash -and
                $previous.status -in @('both-drafts-download-hash-verified','published')
            if ((-not $remote.draft -and -not $resumePublished) -or $remote.body.Trim() -ne $body.Trim()) {
                throw 'Existing published/foreign release refused. Nothing will be overwritten.'
            }
        } elseif ($Mode -eq 'Draft') {
            $remote=Api "repos/$repo/releases" 'Post' @{tag_name=$definition.tag;target_commitish=$definition.commit;
                name=$definition.name;body=$body;draft=$true;prerelease=$true}
            Write-Host "Created draft: $repo/$($remote.id)"
        } else { throw 'Missing draft release.' }
        if ($Mode -eq 'Draft') {
            $existing=Api "repos/$repo/releases/$($remote.id)/assets?per_page=100"
            foreach ($asset in $definition.assets) {
                $name=[IO.Path]::GetFileName($asset.path)
                $match=@($existing|Where-Object name -eq $name)
                if ($match.Count) {
                    if ($match.Count -ne 1 -or $match[0].state -ne 'uploaded' -or $match[0].size -ne $asset.bytes -or
                        ($match[0].digest -and $match[0].digest -ne ('sha256:'+$asset.sha256))) { throw 'Existing draft asset mismatch; replacement is forbidden.' }
                    continue
                }
                Write-Host "Uploading $repo/$name ($($asset.bytes) bytes)"
                $url=($remote.upload_url -replace '\{.*$','')+'?name='+[Uri]::EscapeDataString($name)
                if ($url -notlike 'https://uploads.github.com/*') { throw 'Unexpected upload origin.' }
                $response=Invoke-WebRequest -Uri $url -Headers $headers -Method Post -InFile $asset.path -ContentType 'application/octet-stream' -TimeoutSec 3600
                $uploaded=$response.Content|ConvertFrom-Json
                if ($uploaded.size -ne $asset.bytes -or $uploaded.state -ne 'uploaded') { throw 'Upload failed validation.' }
            }
        }
        Verify-RemoteAssets $definition $remote
        $remoteReleases[$repo]=$remote
    }
    [IO.File]::WriteAllText($journalFile,(@{planSha256=$planHash;status='both-drafts-download-hash-verified';
        releases=@($remoteReleases.Values|ForEach-Object {@{id=$_.id;url=$_.html_url}})}|ConvertTo-Json -Depth 8))
    if ($Mode -eq 'Publish') {
        foreach ($definition in $spec.releases) {
            $remote=$remoteReleases[$definition.repository]
            if (-not $remote.draft) { Write-Host "Already published with verified matching assets: $($remote.html_url)"; continue }
            $published=Api "repos/$($definition.repository)/releases/$($remote.id)" 'Patch' @{draft=$false;prerelease=$true;make_latest='false'}
            if ($published.draft -or -not $published.prerelease) { throw 'Publication did not return a prerelease.' }
            Write-Host "Published: $($published.html_url)"
        }
        [IO.File]::WriteAllText($journalFile,(@{planSha256=$planHash;status='published';
            urls=@($remoteReleases.Values|ForEach-Object html_url)}|ConvertTo-Json -Depth 8))
    }
} finally {
    $headers.Clear(); $credential.Clear(); $raw=$null
}
