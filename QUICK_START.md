# KikiEmu 0.1 Alpha — Windows ARM64

This is a candidate guide. Public release and system acceptance are still
pending. Do not substitute a development disk/bundle for the system ZIP.

## What to obtain

1. `setup.exe` from the KikiEmu release.
2. `KikiAOSP-0.1.0-alpha-arm64.zip` from the kikiaosp_test release.
3. A compatible, privately deployed **native ARM64 patched QEMU bin directory**.
   QEMU is user-provided; setup does not install it or an Android disk.

Requires Windows 11 ARM64 with Windows Hypervisor Platform, a compatible host
OpenGL/VirGL path, and adequate free storage and host memory. The accepted test
hardware is the Snapdragon Surface. Other hardware has not been certified.
WHPX and a real system boot are required; byte/architecture checks alone cannot
prove GPU or driver compatibility. See the repository README for the exact
native MSYS2 CLANGARM64 QEMU build/export recipe and prerequisites.

## Install and initialize

Run setup.exe. It installs for the current user, without elevation, in
`%LOCALAPPDATA%\Programs\KikiEmu`, adds its CLI directory to your user PATH,
and creates Start Menu/desktop entries. Open a **new** PowerShell window:

```powershell
kikiemu create --system ~/Downloads/KikiAOSP-0.1.0-alpha-arm64.zip --storage ~/MyAndroid --size 200g --qemu ~/Tools/KikiQemu/bin
kikiemu list
kikiemu set --default 01
```

`--system`, `--storage`, `--size` and `--qemu` are required. The storage folder
must be NEW. `--performance default|medium|high` is optional. `--create` is an
alias for `create`. `g` means GiB. Choose your own total size; 200g is an example,
not a fixed limit. The total includes the system and all partitions/reserves,
and is immutable after creation. The standalone QCOW2 grows physically as data
is written; it is not preallocated. Host use, decimal GB displayed by Android,
filesystem overhead and free data space are not the same quantity.

After successful creation, the downloaded ZIP is no longer needed. APKs,
photos and settings persist in the chosen storage. Double-click KikiEmu to
start the configured default with the native boot logo/logs, then Android.
No initialization/default or an incompatible runtime produces an English
error dialog; the launcher never silently selects another system or QEMU.

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

`adb --shell` is a bounded, one-shot command against this instance's private
transport, not a global ADB server or an interactive shell. Release and Dev
instances have distinct identities/endpoints. Never use generic QEMU-name
kills or global ADB commands to manage installed instances.

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
UI/input, GPU/120 Hz, audio and front/rear camera behavior. Do not publish or
retag unaccepted candidate bytes. Unsigned Alpha binaries may trigger Windows
SmartScreen; no signing certificate or bypass of Windows security is bundled.
