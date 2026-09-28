# KikiAOSP high-refresh support

## Configuration

The `kikiaosp_test` device overlay sets Android's `config_defaultRefreshRate` to
60 and `config_defaultPeakRefreshRate` to 120. The separate Settings overlay
enables the built-in `config_show_smooth_display` control. This keeps a 60-Hz
baseline while allowing Android's adaptive policy and the Smooth Display
preference to use the Surface's 120-Hz capability when requested. The guest
mode is not removed or capped at 60; the device can switch to 120. No AOSP
upstream source file is edited.

The local GTK QEMU patch `patches/qemu-gtk-guest-refresh-rate.patch` adds the
`KIKI_GTK_GUEST_REFRESH_RATE_HZ` override (30–120 Hz). It uses the same chosen
rate both for VirtIO-GPU's guest-facing display information and QEMU's scanout
poll interval; at 120 Hz, the integer-millisecond poll interval is 8 ms. The
launcher exposes this as `-GuestRefreshRateHz` and defaults it to 120 for GTK.
This avoids advertising a 120-Hz guest mode while continuing to poll scanout
only at the lower GDK-reported desktop cadence.

## Reproducing the Android image

From the `kikiaosp_test` repository, sync and audit the device tree into the
Android 17 checkout, then use the normal incremental product build:

```sh
scripts/sync-device-tree.sh ~/aosp-master
scripts/audit-device-tree-profile.sh
scripts/audit-aosp-integration.sh ~/aosp-master
cd ~/aosp-master
source build/envsetup.sh
lunch kikiaosp_test_arm64_phone-trunk_staging-userdebug
m -j8 systemimage vendorimage
```

When adding a new resource file beneath the copied overlay, ensure Soong
regenerates its glob/build graph before relying on an incremental image build.
The sync copies directory timestamps; a metadata-only `touch` of the affected
overlay resource directory followed by `m nothing` is sufficient to request
glob regeneration when a new file was otherwise missed. Confirm the packaged
resource values with `aapt2 dump resources -v` before copying the image.

## Reproducing the QEMU side

Build the Windows ARM64 QEMU as documented in the root README. The build script
applies the high-refresh patch. For a local GTK run, set `-GuestRefreshRateHz
120` (the default) alongside the verified VirGL/GPU-blob device tuple. The
launcher prints the selected guest refresh rate and passes it to QEMU's GTK
process environment.

## Initial fixed-120 policy validation — 2026-09-28

- `framework-res.apk`: default and peak refresh values both 120.
- `Settings.apk`: `config_show_smooth_display=true`; the Display page visibly
  exposes the checked **Smooth display** switch and says it raises refresh up
  to 120 Hz for some content.
- QEMU booted the image at 864×1728, 8 vCPUs, 4 GiB, VirGL/blob, Client HWC,
  and `-snapshot`. Android reached `sys.boot_completed=1`. `dumpsys display`
  reported `renderFrameRate=120.00001`, active render rate 120.00001, and
  supported rates including 120; SurfaceFlinger reported an 8,333,333-ns
  refresh period.
- New system image SHA-256:
  `9787bee861222847f9f0b24d082c790095f66893e400935203a759b6eabe892a`.
- The local QEMU test candidate was built with the new patch (SHA-256
  `20146b22e0e0b3fb75329c8e06b92c81f865234a37a7e1c30138276e685ff7a7`). The
  pre-existing stable QEMU executable was left untouched.
- Full desktop evidence (2880×1920):
  `C:\Users\keke\Downloads\temp\kikiaosp-high-refresh-display-physical.png`.

These results establish an enabled 120-Hz Android display mode and a QEMU path
configured to communicate/poll at that rate. They do **not** claim that every
application continuously renders 120 fps or that the Surface's instantaneous
physical scanout was independently measured at 120 Hz. During the Settings
scroll sample, QEMU's callback interval median was about 19 ms; scene
performance and host physical-panel telemetry remain separate questions.

Logs and image are retained in `bundles/gpu-virgl-native-20260927/` under the
`high-refresh-20260928` tag.

## Adaptive default/peak policy update — 2026-09-28

The performance investigation showed that forcing the baseline to 120 increased
missed-frame and jank proxies in the tested ADB-driven workloads. This did not
prove a physical touch-to-photon regression, and it is not a reason to drop
120-Hz support. The device-tree policy was therefore changed on branch
`feature/gpu-virgl-20260927` (commit `7b3803a`) to the adaptive 60-Hz baseline /
120-Hz peak while leaving Smooth Display enabled.

- Built `systemimage` incrementally on 192.168.2.185 with `m -j8 systemimage`;
  Soong completed successfully in 13m52s. The resulting image SHA-256 is
  `b1e4f1b481419f49742eb070f5829097b100d2ba42a8bf46e69537e698b64d3f` and is
  retained as `bundles/gpu-virgl-native-20260927/system-adaptive-60hz-peak120-20260928.img`.
- `aapt2 dump resources` verified the packaged framework values are
  `config_defaultRefreshRate=60` and `config_defaultPeakRefreshRate=120`;
  `Settings.apk` contains `config_show_smooth_display=true`. The Settings
  controller checks the highest display mode and falls back to the framework
  peak value, so with a 120-Hz reported mode the built-in switch is available
  and defaults checked.
- Booted the new image with the fencefix kernel, matching HWC vendor, native
  GTK/WGL + VirGL/blob, Client HWC, 8 vCPUs, 4 GiB, 864×1728, and QEMU guest
  refresh override 120. Android reached `sys.boot_completed=1`; SurfaceFlinger
  stayed running. `dumpsys display` reported supported rates including
  `120.00001`, with the normal active render rate at `60.000004`.
- In the disposable `-snapshot` guest, setting both `min_refresh_rate` and
  `peak_refresh_rate` to 120 changed `mActiveRenderFrameRate` to
  `120.00001`; deleting those temporary settings restored 60 while the 120-Hz
  supported mode and 120-Hz peak remained. The Settings Display activity was
  launched; the snapshot was then discarded.
- An initial run accidentally enabled the optional `-AngleEgl` host path and
  caused `GrGLMakeNativeInterface() failed` / SurfaceFlinger restarts. Both the
  prior-system control and adaptive-image run succeeded using the archived
  native GTK/WGL path without `-AngleEgl`; do not add that flag to this profile.
- The Windows host refresh mode was not changed for this run. This validates
  Android/QEMU guest 120-Hz mode selection, not an independent physical-panel
  scanout measurement. The host-side 120-Hz PresentMon experiment below remains
  separate evidence.

Logs are retained under the `adaptive-60-peak120-*` tags in
`bundles/gpu-virgl-native-20260927/`. The test QEMU process was stopped, its
`-snapshot` writes were discarded, and ports 4447/5555 were released.

## Windows host output check (initial fixed-120 build)

The Surface driver exposes 2880×1920 modes at 30, 48, 60, 75, 100, and 120 Hz.
At the start of this check Windows was using 60 Hz. A non-persistent
`ChangeDisplaySettingsEx` test selected 2880×1920@120 Hz successfully; the
selection was verified while active and restored to the original 60-Hz mode in
a `finally` block. The emulator launcher does not change the host's global
refresh setting. To exercise the physical high-refresh path, Windows must
currently select 120 Hz (or its Dynamic option); Android's 120-Hz guest mode
cannot force the host desktop to change its mode.

While the host was temporarily at 120 Hz, PresentMon captured 1,675 frames from
the QEMU process, all `DXGI / Composed: Flip`. `DisplayedTime` included 155
samples around 8.3 ms, consistent with frames entering a 120-Hz presentation
cadence; its median was 16.67 ms and long-tail P95/P99 were 41.67/58.34 ms, so
this is not evidence of stable 120-fps rendering. In the same 35-second ADB
navigation workload, QEMU GTK callbacks averaged 42.73 fps (median active
five-second window 46.11 fps), and SurfaceFlinger added 1,392 GPU-classified
misses, zero HWC-classified misses. Launcher3/Settings/SystemUI reported
33.36%/35.28%/29.87% janky frames. PresentMon's input-to-photon columns were
`NA` because the workload used ADB, not physical touch.

This confirms the Windows/QEMU composed-present path can use the selected
120-Hz desktop mode, but does not show a performance fix or independently
measure panel scanout with an external sensor. The host was restored to 60 Hz;
the QEMU `-snapshot` test instance was closed. Capture:
`C:\Users\keke\Downloads\temp\presentmon-kikiaosp-120hz-host-output-20260928.csv`.
