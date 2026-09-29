param(
  [string]$Serial = '127.0.0.1:5555',
  [Parameter(Mandatory = $true)][string]$LogPath,
  [ValidateRange(864, 3840)][int]$DisplayWidthPixels = 1003,
  [ValidateRange(864, 3840)][int]$DisplayHeightPixels = 1556,
  [ValidateRange(120, 640)][int]$DisplayDensityDpi = 288,
  [ValidateRange(0.5, 2.0)][double]$FontScale = 1.5,
  [ValidateRange(30, 120)][int]$GuestRefreshRateHz = 120,
  [ValidateRange(30, 600)][int]$TimeoutSeconds = 240
)

$ErrorActionPreference = 'Stop'
$adbPath = (Get-Command adb -ErrorAction Stop).Source
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)

function Invoke-Adb {
  param([string[]]$AdbArgumentList)
  $result = & $script:adbPath @AdbArgumentList 2>&1
  if ($LASTEXITCODE -ne 0) {
    throw "adb $($AdbArgumentList -join ' ') failed: $result"
  }
  $result
}

function Invoke-GuestShell {
  param([string[]]$ShellArgumentList)
  Invoke-Adb -AdbArgumentList (@('-s', $script:Serial, 'shell') + $ShellArgumentList)
}

Start-Transcript -LiteralPath $LogPath -Force | Out-Null
try {
  "Waiting for Android on $Serial"
  Invoke-Adb -AdbArgumentList @('connect', $Serial)
  while ([DateTime]::UtcNow -lt $deadline) {
    $stateOutput = & $script:adbPath -s $Serial get-state 2>$null
    $stateExit = $LASTEXITCODE
    if ($stateExit -eq 0 -and ($stateOutput | Out-String).Trim() -eq 'device') {
      $bootOutput = & $script:adbPath -s $Serial shell getprop sys.boot_completed 2>$null
      $bootExit = $LASTEXITCODE
      if ($bootExit -eq 0 -and ($bootOutput | Out-String).Trim() -eq '1') { break }
    }
    Start-Sleep -Seconds 3
  }
  if ([DateTime]::UtcNow -ge $deadline) {
    throw "Android did not finish booting within $TimeoutSeconds seconds"
  }

  'Configuring a virtual, fully charged battery and virtual AC input'
  # Apply power and charge before exposing a battery; otherwise Android can
  # see a newly-present 0% battery and request shutdown between updates.
  Invoke-GuestShell -ShellArgumentList @('dumpsys', 'battery', 'set', '-f', 'ac', '1')
  Invoke-GuestShell -ShellArgumentList @('dumpsys', 'battery', 'set', '-f', 'level', '100')
  Invoke-GuestShell -ShellArgumentList @('dumpsys', 'battery', 'set', '-f', 'status', '5')
  Invoke-GuestShell -ShellArgumentList @('dumpsys', 'battery', 'set', '-f', 'temp', '250')
  Invoke-GuestShell -ShellArgumentList @('dumpsys', 'battery', 'set', '-f', 'present', '1')
  Invoke-GuestShell -ShellArgumentList @('settings', 'put', 'global', 'stay_on_while_plugged_in', '7')
  Invoke-GuestShell -ShellArgumentList @('settings', 'put', 'secure', 'screensaver_enabled', '0')
  Invoke-GuestShell -ShellArgumentList @('locksettings', 'set-disabled', 'true')
  'Restoring the Surface display profile: {0}x{1}, {2} dpi, font scale {3}' -f $DisplayWidthPixels, $DisplayHeightPixels, $DisplayDensityDpi, $FontScale
  Invoke-GuestShell -ShellArgumentList @('wm', 'size', "$($DisplayWidthPixels)x$($DisplayHeightPixels)")
  Invoke-GuestShell -ShellArgumentList @('wm', 'density', [string]$DisplayDensityDpi)
  $fontScaleValue = $FontScale.ToString([Globalization.CultureInfo]::InvariantCulture)
  Invoke-GuestShell -ShellArgumentList @('settings', 'put', 'system', 'font_scale', $fontScaleValue)
  $refreshValue = '{0}.0' -f $GuestRefreshRateHz
  'Setting the guest-wide refresh range to {0} Hz' -f $GuestRefreshRateHz
  Invoke-GuestShell -ShellArgumentList @('settings', 'put', 'system', 'peak_refresh_rate', $refreshValue)
  Invoke-GuestShell -ShellArgumentList @('settings', 'put', 'system', 'min_refresh_rate', $refreshValue)
  Invoke-GuestShell -ShellArgumentList @('wm', 'dismiss-keyguard')
  Invoke-GuestShell -ShellArgumentList @('input', 'keyevent', '3')
  $refreshDeadline = [DateTime]::UtcNow.AddSeconds(15)
  $activeRefreshRate = $null
  do {
    $display = (Invoke-GuestShell -ShellArgumentList @('dumpsys', 'display') | Out-String)
    $activeMatch = [regex]::Match($display, 'mActiveRenderFrameRate=([0-9]+(?:\.[0-9]+)?)')
    if ($activeMatch.Success) {
      $activeRefreshRate = [double]::Parse($activeMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
      if ([Math]::Abs($activeRefreshRate - $GuestRefreshRateHz) -lt 0.5) { break }
    }
    Start-Sleep -Seconds 1
  } while ([DateTime]::UtcNow -lt $refreshDeadline)

  $battery = (Invoke-GuestShell -ShellArgumentList @('dumpsys', 'battery') | Out-String)
  $power = (Invoke-GuestShell -ShellArgumentList @('dumpsys', 'power') | Out-String)
  $lockDisabled = (Invoke-GuestShell -ShellArgumentList @('locksettings', 'get-disabled') | Out-String).Trim()
  $minRefresh = (Invoke-GuestShell -ShellArgumentList @('settings', 'get', 'system', 'min_refresh_rate') | Out-String).Trim()
  $peakRefresh = (Invoke-GuestShell -ShellArgumentList @('settings', 'get', 'system', 'peak_refresh_rate') | Out-String).Trim()
  if ($battery -notmatch 'AC powered: true') { throw 'Android did not accept the virtual AC power state' }
  if ($battery -notmatch 'present: true' -or $battery -notmatch 'level: 100' -or $battery -notmatch 'status: 5') {
    throw 'Android did not accept the virtual full-battery state'
  }
  if ($power -notmatch 'mStayOn=true') { throw 'PowerManager did not enter stay-awake mode' }
  if ($lockDisabled -ne 'true') { throw 'Android lock screen is still enabled' }
  if ($minRefresh -ne $refreshValue -or $peakRefresh -ne $refreshValue) {
    throw "Android did not accept the $GuestRefreshRateHz Hz min/peak refresh settings (min=$minRefresh peak=$peakRefresh)"
  }
  if ($null -eq $activeRefreshRate -or [Math]::Abs($activeRefreshRate - $GuestRefreshRateHz) -ge 0.5) {
    throw "Android did not switch its active render rate to $GuestRefreshRateHz Hz; active=$activeRefreshRate. Check that QEMU advertises the requested mode."
  }

  'DISPLAY_READY: guest_refresh={0}Hz active_render_rate={1:N2}Hz virtual_battery_full=true virtual_AC=true stay_awake=true lock_screen_disabled=true HOME_requested=true' -f `
    $GuestRefreshRateHz, $activeRefreshRate
} catch {
  "DISPLAY_SETUP_FAILED: $_"
  exit 1
} finally {
  Stop-Transcript | Out-Null
}
