# KikiEmu 0.1 Alpha — Windows ARM64

This is the 0.1 Alpha guide. Do not substitute a development disk/bundle for
the clean system ZIP. See [release details and limitations](RELEASE_0_1_ALPHA.md).

## What to obtain

1. `setup.exe` from the [KikiEmu release](https://github.com/kekeqwq/KikiEmu/releases/tag/v0.1.0-alpha).
2. `KikiAOSP-0.1.0-alpha-arm64.zip` from the [kikiaosp_test release](https://github.com/kekeqwq/kikiaosp_test/releases/tag/v0.1.0-alpha).
3. A compatible, privately deployed **native ARM64 patched QEMU bin directory**.
   QEMU is user-provided; setup does not install it or an Android disk.

Requires Windows 11 ARM64 with Windows Hypervisor Platform, a compatible host
OpenGL/VirGL path, and adequate free storage and host memory. The accepted test
hardware is the Snapdragon Surface. Other hardware has not been certified.
WHPX and a real system boot are required; byte/architecture checks alone cannot
prove GPU or driver compatibility. [Build QEMU independently](QEMU_BUILD.md):
clone only upstream QEMU, enter it, download `build.ps1`, and run it with your
MSYS2 path. No KikiEmu clone is required. Original output stays in QEMU's
`bin`; the build script does not deploy DLLs/ROMs. Pass bin directly to
`create`: KikiEmu prepares missing DLLs/ROMs automatically from the build's
recorded MSYS2/source paths. Complete runtimes are left unchanged. You do not
run prepare.ps1 or clone KikiEmu. Keep the matching MSYS2 installation and
QEMU pc-bios until preparation is complete.

## Install and initialize

Run setup.exe. It installs for the current user, without elevation, in
`%LOCALAPPDATA%\Programs\KikiEmu`, adds its CLI directory to your user PATH,
and creates Start Menu/desktop entries. Open a **new** PowerShell window:

```powershell
kikiemu create --system ~/Downloads/KikiAOSP-0.1.0-alpha-arm64.zip --storage ~/MyAndroid --size 200g --qemu ~/Repos/qemu/bin
kikiemu list
kikiemu set --default 01
```

`--system`, `--storage`, `--size` and `--qemu` are required. The storage folder
must be NEW. `--performance default|medium|high` is optional. `--create` is an
alias for `create`. `g` means GiB. Choose your own total size; 200g is an example,
not a fixed limit.

`~/` and `~\` paths expand inside KikiEmu to your current user profile. Quote
paths containing spaces. create prints live validation, extraction, import
and disk-readback progress; success is reported only after registration.
The total includes the system and all partitions/reserves,
and is immutable after creation. The standalone QCOW2 grows physically as data
is written; it is not preallocated. Host use, decimal GB displayed by Android,
filesystem overhead and free data space are not the same quantity.

Examples assume the new instance printed `Created id 01`. If retained instances
already exist, use the actual new ID printed by creation for default/set/start
and ADB; do not accidentally select an older instance.

After successful creation, the downloaded ZIP is no longer needed. APKs,
photos and settings persist in the chosen storage. Double-click KikiEmu to
start the configured default with the native boot logo/logs, then Android.
No initialization/default or an incompatible runtime produces an English
error dialog; the launcher never silently selects another system or QEMU.

Closing the Android window requests a normal Android shutdown. Wait for the
window to disappear while data is saved; do not kill QEMU or power off Windows.
The final Alpha requires the five-patch QEMU recipe in [QEMU_BUILD.md](QEMU_BUILD.md),
including managed window-close. If updating an earlier test candidate, rebuild
QEMU, install the new setup and revalidate with `kikiemu set --id 01 --qemu PATH`.
Existing disks need not be recreated. Photos already reduced to zero-filled
files by an earlier candidate cannot be restored by this update.

Creation can take several minutes while validating and reading back the disk.
Wait for `Created id NN`; do not start a second creation into the same folder.
If creation is interrupted before registration, keep that folder for diagnosis
and choose another NEW folder for a retry. Full crash/interruption recovery is
not certified in this candidate; never reformat established data to recover it.

## Manage an instance

```powershell
kikiemu set --id 01 --mem 8g
kikiemu set --id 01 --cpus 8
kikiemu set --id 01 --qemu ~/Tools/KikiQemu-v2/bin
kikiemu start --id 01
kikiemu logs --id 01
kikiemu adb --id 01 --shell "getprop ro.serialno"
kikiemu stop --id 01
```

Settings changes take effect on the **next** start. No disk resize, system
replacement, arbitrary QEMU flags or live resource hotplug is provided in 0.1.
The default preset keeps the tested 8 vCPU/4 GiB, SDL/VirGL/120 Hz and
1003×1556 native-pixel window. Guest 120 Hz support is not a guarantee of
120 FPS; remaining input/render latency is a known Alpha limitation.

`kikiemu adb --shell` is a bounded, one-shot command against this instance's
transport, not an interactive shell. It does not register a connection in the
Platform Tools ADB server, so an empty `adb devices` list does not mean that
Android or adbd failed to start.

## Connect ordinary Android Platform Tools ADB

Install Android Platform Tools separately if you need `adb shell`, APK
installation, or file transfer. With instance 01 **running**, open PowerShell:

```powershell
$instance = kikiemu info --id 01 | ConvertFrom-Json
if ($LASTEXITCODE -ne 0) { throw 'Could not read instance 01.' }
$endpoint = @($instance.runtime.endpoints | Where-Object role -eq 'adb')
if ($endpoint.Count -ne 1) { throw 'Start instance 01 and wait for Android first.' }
$serial = '127.0.0.1:' + $endpoint[0].port
adb connect $serial
adb devices -l
adb -s $serial shell
```

The port is allocated on **each start**. Do not assume port 5555 or reuse a
port from a previous boot. Run the commands above again after restarting the
instance. Always use `-s $serial` if more than one device is listed.

Examples after connecting (run these in the host PowerShell, not inside the
Android shell):

```powershell
adb -s $serial install "$HOME/Downloads/example.apk"
adb -s $serial pull /sdcard/Pictures "$HOME/Pictures/Android"
adb -s $serial shell getprop ro.serialno
adb disconnect $serial
```

Connecting does not require a new image, reinstallation or an ADB-server
restart. Do not use `adb kill-server` or process-name kills to manage a VM.
The built-in `kikiemu adb --id 01 --shell "COMMAND"` remains available without
Platform Tools and selects the recorded instance directly.

## Future development and system versions

As agreed on 2026-10-01, subsequent fixes are built and tested as ordinary
versioned packages in the **same format**, then advance to 0.2, 0.3 and so on.
A separate Dev channel/registry/launcher and mandatory release/Dev concurrency
test are no longer required. The existing per-instance UUID, process and
endpoint checks remain: this is simpler development, not permission to stop
unrelated processes or overwrite established storage.

Test a new system ZIP in a NEW storage folder with `create`, and select its
explicit instance ID. In 0.1, `set` changes resources/QEMU only; it does not
replace an installed OS or resize a disk. Published assets remain immutable;
a repair is a new version, not new bytes under an old version's filename.

## Delete versus uninstall

```powershell
kikiemu --delete --force --id 01
```

This does **not** ask again: it kills only this registered instance's runtime,
permanently removes its entire storage/data, unregisters it and clears its
default selection if necessary. There is no undo. An ownership/path/process
mismatch refuses deletion rather than redirecting it. Other instances, the
downloaded ZIP, QEMU and the launcher installation are not deleted.

Uninstall KikiEmu using Windows Apps or its Start Menu entry. Close its
instances first: setup/uninstall refuses to overwrite an in-use installation
and never kills a running VM automatically. Uninstall removes the app's own
files, PATH entry and shortcuts, **not** system storage, instance records,
QEMU or ZIP files. Reinstall preserves the instance records.

## Alpha testing

The user tests installation/uninstallation, PATH, shortcuts and public CLI
flows. Internal build/library checks are not substitutes for that acceptance.
The agent separately verifies the clean system's boot, persistent capacity,
UI/input, GPU/120 Hz, audio and front/rear camera behavior. Published assets
are immutable; fixes use a new version. Unsigned Alpha binaries may trigger Windows
SmartScreen; no signing certificate or bypass of Windows security is bundled.
