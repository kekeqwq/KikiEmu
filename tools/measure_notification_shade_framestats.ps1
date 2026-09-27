param(
  [ValidateRange(1, 8)][int]$Cycles = 3,
  [string]$AdbSerial = '127.0.0.1:5555'
)

$ErrorActionPreference = 'Stop'

function Invoke-Adb([string[]]$Arguments) {
  $output = @(& adb -s $AdbSerial @Arguments 2>&1)
  if ($LASTEXITCODE -ne 0) { throw "adb $($Arguments -join ' ') failed: $($output -join ' ')" }
  return $output
}

function Get-Percentile([double[]]$Values, [double]$Fraction) {
  if ($Values.Count -eq 0) { return [double]::NaN }
  $sorted = @($Values | Sort-Object)
  return $sorted[[Math]::Max(0, [int][Math]::Ceiling($Fraction * $sorted.Count) - 1)]
}

$size = (Invoke-Adb -Arguments @('shell', 'wm size')) -join ' '
$sizeMatch = [regex]::Match($size, 'Physical size:\s*(\d+)x(\d+)')
if (-not $sizeMatch.Success) { throw "Cannot verify guest size: $size" }
$width = [int]$sizeMatch.Groups[1].Value
$height = [int]$sizeMatch.Groups[2].Value
if ([Math]::Min($width, $height) -lt 864 -or [Math]::Max($width, $height) -lt 1728) {
  throw "Below the 864x1728 floor: ${width}x${height}"
}
if (((Invoke-Adb -Arguments @('shell', 'getprop sys.boot_completed')) -join '').Trim() -ne '1') {
  throw 'Android has not completed boot.'
}

Invoke-Adb -Arguments @('shell', 'input keyevent HOME') | Out-Null
Start-Sleep -Milliseconds 500
Invoke-Adb -Arguments @('shell', 'dumpsys gfxinfo com.android.systemui reset') | Out-Null
$centerX = [int]($width / 2)
$bottomY = [int]($height * 0.64)
for ($cycle = 0; $cycle -lt $Cycles; $cycle++) {
  Invoke-Adb -Arguments @('shell', "input swipe $centerX 2 $centerX $bottomY 450") | Out-Null
  Start-Sleep -Milliseconds 250
  Invoke-Adb -Arguments @('shell', "input swipe $centerX $bottomY $centerX 2 450") | Out-Null
  Start-Sleep -Milliseconds 250
}
$lines = @(Invoke-Adb -Arguments @('shell', 'dumpsys gfxinfo com.android.systemui framestats'))
$inShade = $false
$inProfile = $false
$columns = @()
$rows = [Collections.ArrayList]::new()
foreach ($line in $lines) {
  if ($line -match '^Window: NotificationShade\s*$') {
    $inShade = $true
    continue
  }
  if (-not $inShade) { continue }
  if ($line -match '^---PROFILEDATA---\s*$') {
    if ($inProfile) { break }
    $inProfile = $true
    continue
  }
  if (-not $inProfile) { continue }
  if ($line -match '^Flags,') {
    $columns = @($line.TrimEnd(',').Split(','))
    continue
  }
  if ($columns.Count -eq 0 -or $line -notmatch '^\d+,') { continue }
  $fields = @($line.TrimEnd(',').Split(','))
  if ($fields.Count -ne $columns.Count) { continue }
  $row = @{}
  for ($index = 0; $index -lt $columns.Count; $index++) {
    $row[$columns[$index]] = [long]$fields[$index]
  }
  if ($row.Flags -eq 0 -and $row.FrameCompleted -gt $row.IntendedVsync) {
    [void]$rows.Add($row)
  }
}
if ($rows.Count -eq 0) { throw 'No valid NotificationShade framestats rows; check that the shade animated.' }

$stages = @(
  @('IntendedVsync', 'HandleInputStart'),
  @('HandleInputStart', 'PerformTraversalsStart'),
  @('PerformTraversalsStart', 'DrawStart'),
  @('DrawStart', 'SyncQueued'),
  @('SyncQueued', 'SyncStart'),
  @('SyncStart', 'IssueDrawCommandsStart'),
  @('IssueDrawCommandsStart', 'SwapBuffers'),
  @('SwapBuffers', 'FrameCompleted'),
  @('IntendedVsync', 'FrameCompleted')
)
Write-Output "SHADE_FRAMESTATS guest=${width}x${height} cycles=$Cycles valid_frames=$($rows.Count) trace=off"
foreach ($stage in $stages) {
  $startName = $stage[0]
  $endName = $stage[1]
  $values = [double[]]@(
    foreach ($row in $rows) {
      if ($row[$startName] -gt 0 -and $row[$endName] -ge $row[$startName]) {
        ($row[$endName] - $row[$startName]) / 1000000.0
      }
    }
  )
  if ($values.Count -eq 0) { continue }
  Write-Output ('STAGE {0}->{1} count={2} median={3:N2}ms p95={4:N2}ms max={5:N2}ms over_33ms={6}' -f `
      $startName, $endName, $values.Count, (Get-Percentile $values 0.50),
      (Get-Percentile $values 0.95), (Get-Percentile $values 1.00),
      @($values | Where-Object { $_ -gt 33.3 }).Count)
}
Write-Output 'Framestats are per NotificationShade HWUI frame, not Windows DWM presents or a direct touch-to-photon measurement.'
