# KikiAOSP stable GUI baseline

The verified Windows run reached `sys.boot_completed=1` and presented the animated
`KikiWindowTest` activity for at least 120 seconds. This is a minimal GUI baseline;
it does not include the experimental input-device arguments that previously caused
instability.

## Source revisions

- AOSP/device baseline: `kekeqwq/kikiaosp_test`, branch
  `baseline/stable-animated-ui-20260926`, commit
  `9ae7f33a71cc20c94559ed498677a48c1318415e`.
- Windows GTK repaint fix: preserved in `qemu-windows-gtk-full-redraw.patch`;
  it corresponds to local QEMU commit `bde658eef6b38c45794bfd7ad4d2dd1b574e4694`.

The QEMU patch is downstream and must be applied to the local QEMU checkout, not
pushed to the upstream QEMU repository.

## Run

With the prepared images under `aosp/windows-arm64-test` and the patched QEMU binary
at `tools/qemu-src/build/qemu-system-aarch64.exe`, start the known-good pairing:

```powershell
.\tools\run_kikiaosp_ui_local.ps1
```

The script defaults to the verified RC4 kernel, system image, vendor image, and
audio stub-output workaround. Each run gets a unique log tag. It deliberately does
not attach experimental input devices. UART and guest logcat are saved beside the
images; the QEMU monitor listens only on `127.0.0.1:4447`, and ADB forwarding uses
`127.0.0.1:5555`.

The images themselves are local build artifacts and are not stored in this source
repository.
