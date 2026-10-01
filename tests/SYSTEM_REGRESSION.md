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
./tools/capture_system_regression.ps1 -Runner $runner -FixtureRoot $fixture -Tag clean32 -Activate
# If Windows denies activation while the user is working, -Unobscured can
# briefly raise only the exact owned window without activation/input hooks;
# its previous non-topmost state is restored after the full-desktop capture.
./tools/capture_system_regression.ps1 -Runner $runner -FixtureRoot $fixture -Tag clean32 -Unobscured
# For the separate stable resize/maximized comparison, explicitly add -Maximize.

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

The dedicated screenshot tool resolves ONLY the runner's verified test
fixture, requires its running/readiness state and pins the exact recorded
QEMU creation/path/hash identity. It selects that PID's visible SDL window,
never a console, title-only/default/newest QEMU or a public user's instance.
It captures the entire physical Windows virtual desktop using a temporary
per-monitor-v2 thread DPI context (including the Surface's actual2880x1920),
not a scaled corner or QEMU framebuffer. Images stay in Downloads/temp and
are never release inputs. Foreground/maximize changes are explicit switches;
no AttachThreadInput, host display mode, keyboard or audio changes are used.
The optional `-Unobscured` switch temporarily changes only this pinned test
window's Z-order, never another app's focus/input. It retains an originally
topmost window's state and restores an originally non-topmost window in
`finally`, including failure paths. Do not call this foreground acceptance;
it is an unobscured physical-desktop observation.

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
   Also capture a real photo, hash/decode its JPEG bytes, close the actual SDL
   window using `--close-window` (WM_CLOSE), then boot again and compare bytes.
   Do NOT call `sync` first: it masks missing guest shutdown. A retained media
   database row or filename/size is insufficient. Repeat at least twice and
   collect a second-boot desktop screenshot before injecting any touch.
4. Real camera front/back switching, orientation, successful capture and
   camera release; network, audio, touch, external keyboard, Settings/files,
   wallpaper, Launcher/SystemUI and resizing regressions as applicable.
5. Hardware acceleration/high-refresh identity plus actual rendered Windows
   evidence and a runtime stability interval, not only PE/compiler markers.
6. Retain target-owned process/endpoint/storage negative checks and identify
   any user-dependent or unperformed observations explicitly. On2026-10-01
   the user withdrew the independent Dev-tool/concurrency gate and chose
   ordinary same-format versioned packages as the future development route.
   This runner does not claim that the former concurrency matrix passed.

Record the matching clean-package fresh boots, actual capacities, persistent
reboots, UI/GPU and applicable functional results before handing off a **test**
ZIP with setup.exe and candidate instructions. Distinguish guest-injected
actions from real host touch/physical-keyboard acceptance; the latter may be
completed by the user alongside installer testing. Installation, PATH,
shortcuts, public create/set/delete/default and uninstallation remain the
user's own acceptance work. Outstanding public-release gates stay explicit;
neither this internal run nor a test handoff is public-release acceptance.

## 2026-10-01 close-button / second-boot regression

The 32-GiB independent fixture used the unchanged clean ZIP SHA-256
`73a184068b2c1b5576add96bcbadf4621bf3c3998c7f01fffeb624ee44aad42a`.
It booted three times with the managed-close/overlay-cleanup patch and the
shared supervisor recipe. Two real Surface-camera captures preceded the
first SDL WM_CLOSE; a third preceded the next WM_CLOSE. No test-side `sync`
was injected. Android's serial log recorded `/data` unmount, shutdown sync
and kernel power down. All three JPEG hashes survived; all decoded as
640x480 images using an independent Windows decoder after the third boot.
The second and third boots showed complete HOME in full 2880x1920 Windows
captures before any guest touch/key injection. Exact session UUIDs:
`1d2b4f9d-08b4-4e5f-8a94-b07938158d70`,
`5af12580-d94a-49bd-9173-a582d8c16b64`,
`d34d1221-7288-4e32-bf28-387f8457e8e5`.
Only these owned test windows were closed; the user's installed instance,
QEMU bin, storage and registry were not modified. Updated installer/CLI
acceptance remains the user's responsibility, not covered by this runner.
