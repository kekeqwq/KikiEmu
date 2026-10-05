# Independent audio regression tools

Developer-only; not installed by setup. Use a NEW internal system-regression
fixture, never an installed user's instance or public registry.

## Synthetic Android app

```sh
bash tools/audio-stress/build-on-linux.sh AOSP_SOURCE MATCHING_OUTPUT NEW_APK_OUTPUT
```

Uses the matching AOSP javac/D8/aapt2/signapk and public SDK. No microphone,
network, user media or permissions. `com.kiki.audiostress/.AudioStressActivity`
accepts intent extras `mode`, `count` (1..2000) and `gap` (0..10000 ms):

- `player`: create/release MediaPlayer with varied generated WAV clips.
- `track`: static AudioTrack at six sample rates, mono/stereo.
- `pool`: SoundPool with preloaded clips; completion is only scheduling.
- `hold`: continuous streaming AudioTrack, generated clips separated by PCM
  silence (keeps the HAL active); no decoder involved.

Clips are 100..500 ms with 10-ms ramps. Every 40 clips has a four-second idle
(or PCM silence for `hold`). A final 2-second static track is a separate probe.
Events are logged to `KikiAudioStress` and external-files `events.log`.
The final reference rejects a playback head claiming two seconds in under
one second (a failed persistent stream can otherwise appear to complete).
Do not check MODE_STATIC's state before its first write: it starts in
STATE_NO_STATIC_DATA, not STATE_INITIALIZED.

**Playback callbacks, advancing heads and zero app underruns do not prove host
sound.** Pair every test with the recorder below. Do not run duplicate logcat
collectors against the same file; prefer bounded tag-filtered logs.

## Native Windows ARM64 tools

```powershell
pwsh -NoProfile -File tools/audio-stress/build-native.ps1 `
  -NativeDirectory AUDITED_NATIVE_CORE_DIRECTORY -OutputDirectory NEW_TOOL_DIRECTORY
NEW_TOOL_DIRECTORY/process-loopback.exe FIXTURE_ROOT RUNNER_APP_ROOT SECONDS NEW_WAV
```

Requires MSYS2 CLANGARM64 and Windows build 20348+. Validates the developer
fixture, one-instance/null-default private registry, registration/storage
identity and the exact live QEMU image hash/PID/creation time. The QEMU process
is pinned. Captures only that process tree using Windows process loopback,
not a microphone or all host applications. Read-only session gain/mute/state
are printed. No SetVolume/SetMute/default-device/settings calls.

Produces 48-kHz stereo PCM16 WAV and a packet CSV with QPC timestamps, frame
counts, flags, RMS and peak. **Packet gaps must be considered before interpreting
concatenated WAV phase/duration or frequency-fit residual as distortion.**
Calibrate with a known successful clip in the SAME capture session; process
loopback evidence does not prove physical speaker audibility, nor does one
silent run prove a permanent failure requiring reboot.

`guest-stage.exe FIXTURE_ROOT RUNNER_APP_ROOT CANDIDATE_FILE` is a private HAL
experiment helper. It snapshots a <=32-MiB source, uploads over the existing
owned private shell, and verifies its guest SHA-256 at the fixed destination
`/data/local/tmp/kiki-audio03/audio-service-candidate`. Refuses an existing
candidate. It does NOT mount it, restart services, modify immutable images or
use/start a host ADB daemon. Every command revalidates QEMU/endpoint ownership.
Any runtime overlay is a private experiment, not release-package validation.

`qemu-capture.exe FIXTURE_ROOT RUNNER_APP_ROOT SECONDS` independently captures
the owned QEMU's MIXER output through QMP/HMP. It verifies the exact QEMU and
OS endpoint ownership before each command; refuses any existing capture. A
NEW UUID-named, storage-marked subdirectory under the fixture contains
`ownership.json` and `qemu.wav`. It never accepts an arbitrary monitor/port,
changes a backend/volume or writes outside the private fixture. It stops only
its matching capture and checks final stop status. SIGKILL/tool timeouts can
interrupt cleanup: inspect that run's ownership record before resuming.

This uses deprecated HMP `wavcapture` supported by the tested QEMU commit;
future QEMU may remove it. Mixer capture is upstream of SDL/Windows and can
help distinguish lost guest/backend samples from loopback packet gaps. It is
not physical-speaker proof, and enabling capture itself can affect timing.
Compare to a separate uninstrumented run, not just this one.
