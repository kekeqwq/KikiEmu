# Clean-package real-system regression (developer only)

This is **system testing**, not the user's setup.exe/public CLI/desktop
acceptance. `kikiemu-system-regression.exe` is built from tracked source but
is explicitly excluded from the installer. It never calls the public entry
points, uses their installed registry, modifies PATH/shortcuts, starts a host
ADB daemon, or changes host display/audio/keyboard settings.

It exercises the same bounded ZIP reader, GPT/dynamic QCOW2 installer,
registered process/endpoint checks, native supervisor, private ADB wire and
complete SDL/VirGL/120-Hz boot recipe as the product. Do not use bare QEMU
capability probes on the Surface. Passing metadata fixtures or compiling
this runner is **not** evidence that a real system boots.

## Inputs and namespace

- Use the **actual clean producer ZIP**, its SHA-256 sidecar and provenance.
  Never substitute a NONBOOTABLE fixture, old developer bundle, initialized
  QCOW2/userdata, or independently guessed boot/system/vendor files.
- Use the freshly exported, matching native ARM64 QEMU directory with all
  four tracked patches. Static validation does not certify WHPX/host GPU.
- Build native components and the camera bridge into the same private test
  output directory. Keep this directory unchanged during the regression.
- Every fixture root must be NEW and its parent must exist. The runner creates
  its own root and separate instance ownership markers, internal registry,
  fresh disk and source record. It requires its exact marker, owner/file IDs,
  child path, app path, package hash, fixed capacity and single-instance
  registration on subsequent operations. Public/default/foreign registries
  are refused. This is accidental-operation isolation, not a security boundary
  against the same user deliberately forging all records.
- Even though the internal registry is separate, this runs the **release**
  guest identity/title/boot contract. It does not pretend that a release ZIP
  is a Dev system, or prove release/Dev concurrency by itself.

## Run after the real package is complete

Example PowerShell paths below are internal fixtures, not user-facing install
instructions. `32g` and `200g` are test examples, not fixed storage sizes.

```powershell
$runner = (Resolve-Path ./build/release-system-tests/kikiemu-system-regression.exe).Path
$zip = (Resolve-Path ./build/clean-system-package/KikiAOSP-0.1.0-alpha-arm64.zip).Path
$qemu = (Resolve-Path ./build/qemu-alpha/bin).Path
$fixture = [IO.Path]::GetFullPath('./build/system-regression-32a')

# Validate the REAL ZIP and create a fresh dynamic single-disk test system.
& $runner --prepare $zip $qemu $fixture 32g
if ($LASTEXITCODE -ne 0) { throw 'Internal system preparation failed.' }

# A hidden internal supervisor owns the real QEMU GUI; no second terminal.
# This does not execute the public desktop program or setup.exe.
$supervisor = Start-Process -FilePath $runner -WindowStyle Hidden -PassThru `
    -ArgumentList @('--boot', ('"{0}"' -f $fixture)) `
    -RedirectStandardOutput "$fixture/runner.stdout.log" `
    -RedirectStandardError "$fixture/runner.stderr.log"

& $runner --describe $fixture
& $runner --shell $fixture 'getprop ro.serialno; getprop ro.kikiaosp.build_channel; getprop ro.build.version.release; uname -r'
& $runner --shell $fixture 'blockdev --getsize64 /dev/block/vda; df -k /data; dumpsys window displays; dumpsys display'

# Only after log evidence proves boot/configuration/HOME readiness, collect
# physical full-desktop Windows screenshots and verify complete visible UI.
# Never treat a just-opened black/inactive window as the final boot state.

& $runner --shell $fixture 'printf "%s\n" clean-package-persistence-proof > /data/local/tmp/kiki-system-proof; sync'
& $runner --shutdown $fixture
if ($LASTEXITCODE -ne 0) { throw 'Graceful shutdown failed; inspect the owned logs.' }
$supervisor.WaitForExit()

# Boot the SAME fixture normally (not --prepare, no -snapshot/reformat).
# Repeat the hidden --boot invocation, then:
& $runner --shell $fixture 'cat /data/local/tmp/kiki-system-proof; blockdev --getsize64 /dev/block/vda'
```

The boot command blocks for the lifetime of its exact registered supervisor.
It uses the full paired WHPX/ARM64 kernel/initramfs/GPT/SDL GL/audio/input/camera
recipe, creates unique private endpoints, and records per-session logs under
`instance/logs/SESSION_UUID`. The shared supervisor exposes Android only after
boot completion, exact guest serial/channel, WindowManager HOME focus, actual
SurfaceFlinger HOME layer, 120-Hz rendering, stay-awake and unlocked state pass.
It writes ERROR and keeps diagnostics on failure; do not restart merely
because an observation timed out. `--shell` and `--shutdown` recheck exact
release guest identity before issuing the requested operation.

Shutdown is graceful and target-scoped, with no kill-all or global ADB command.
If QEMU does not exit, the runner reports failure and retains the fixture; it
does not silently kill another instance or delete any disk. Closing the owned
system window also ends this test session. Fixtures stay on disk for explicit
diagnosis/persistence verification; there is no arbitrary-path cleanup mode.

## Required evidence, not just a green test

For at least two independently created capacities, record the actual ZIP,
producer/source-lock/kernel/consumer/QEMU identities and:

1. Fresh boot with the real expected build identity, SDL/VirGL, guest120Hz,
   complete 1003x1556 baseline UI, no Grab and no extra console.
2. Private ADB against actual Android adbd (mock wire tests are insufficient),
   exact disk/block/vold/StorageStats capacity and correct Settings accounting.
3. A written data marker surviving graceful shutdown and normal reboot without
   formatting established data; no dependency on the original ZIP/staging.
4. Real camera front/back switching, orientation, successful capture and
   camera release; network, audio, touch, external keyboard, Settings/files,
   wallpaper, Launcher/SystemUI and resizing regressions as applicable.
5. Hardware acceleration/high-refresh identity plus actual rendered Windows
   evidence and a runtime stability interval, not only PE/compiler markers.
6. Separate release A/Dev B coexistence and wrong-target rejection using the
   guarded Dev tools. This single-instance runner does not cover that gate.

Only after these system results are recorded can the matching **test** ZIP be
handed off with setup.exe and published candidate instructions. Installation,
PATH, shortcuts, public create/set/delete/default and uninstallation remain
the user's own acceptance work; do not relabel this internal run as their test.
