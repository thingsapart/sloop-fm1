# SLOOP Android

An additive Android port of SLOOP with the real twelve engines, sequencer, effects, FM-1 screen, and project storage. All port files live in `ports/android/`; no existing firmware, build, test, or web source is edited. The only firmware adaptations are three checked substitutions in a generated build copy, fixing user sample address arithmetic for ARM64.

The app requires Android 8 or newer. It uses native AAudio, Android Views, and Android MIDI. It has no third-party runtime library and builds without Android Studio, Gradle, Kotlin, or CMake. The initial audio backend renders and updates the firmware UI on one native worker, then uses blocking AAudio writes. This keeps firmware state on one thread and avoids changing interrupt guards across the original code.

## Build an APK

Install Python 3 with Pillow, JDK 17 or 21, and the Android SDK command-line tools. On Linux/macOS, install these packages into an SDK directory of your choice:

```sh
sdkmanager --sdk_root="$ANDROID_SDK_ROOT" 'platforms;android-35' 'build-tools;35.0.0' 'ndk;27.2.12479018' 'platform-tools'
python3 -m pip install Pillow
python3 ports/android/build.py
```

Set `JAVA_HOME` to the JDK if it is not on PATH. Set `ANDROID_SDK_ROOT` to the SDK location; the default is `ports/android/build/sdk`. `ANDROID_NDK_ROOT` can override the NDK path. The script generates assets, compiles ARM64 code, compiles Java, creates a DEX, packages the APK, and signs it with a local debug key. Windows hosts are not currently handled by the build script.

Output: `ports/android/build/sloop-android.apk`.

```sh
adb install -r ports/android/build/sloop-android.apk
adb shell am start -n org.sloop.android/.MainActivity
```

This is a debug APK for sideloading. Keep `build/debug.keystore` if you want subsequent builds to update the same installation. Distribution requires a release signing workflow and appropriate target-SDK review. Native libraries are linked for 16 KiB page compatibility.

For an external copy of the port, set `SLOOP_SOURCE=/path/to/sloop-fm1` before running the build and tests. The port directory can live outside the source checkout. Generated files remain under the port's own `build/`; upstream asset generators may also use the source checkout's ignored `build/` cache.

## Play and record

Use the screen's original SLOOP pages and the virtual controls below/beside it. Hold a function button while playing keys for layers such as punch FX, step entry, erase, and note repeat. Drag SELECT, ALGO, PRESETS, or K1–K4 vertically to turn the corresponding encoder. Tap HOME to select tracks as on FM-1. The keys preserve FM-1's F-to-G keyboard layout and sixteen white-key drum/step positions. Several keys and buttons can be held simultaneously.

The top menu contains MIDI connections, routing, master volume, backup exchange, sample import, and audio diagnostics. Playback stops when SLOOP leaves the foreground or loses audio focus. Reopening the app resumes the working project with transport stopped.

The original four project/section slots, user presets, settings, and working-project autosave use the firmware's existing object formats and A/B storage validation. A 1 MiB memory image backs those objects and is persisted to app-private storage through an atomic file replacement. Saves are flushed periodically while transport is stopped and on app suspension. File I/O currently runs on the engine worker during those flushes, so a flush while live notes still sound may cause an underrun; diagnostics expose the count. Playback-time saving remains a later refinement.

Allow normal suspension to finish before force-stopping the app. An immediate process kill can lose changes that have not reached disk; atomic replacement protects the previous save, not unsaved edits.

Export/restore exchanges **Android memory images**, not the web editor's `.backup` container. Restore replaces the app's projects, settings, banks, and samples. Import user samples as SLOOP FSMP slot images, up to 80 KiB; stop the transport first. Direct WAV conversion and the FM-1 web editor transport are not implemented in this initial port.

## USB and wireless MIDI pads

Connect a class-compliant USB MIDI controller using a USB data cable. The phone must support USB host mode; some combinations need an OTG adapter or powered hub. Open **Connect USB / available MIDI** and select the controller. Android exposes its MIDI output ports to SLOOP; output here means the controller sends notes into the app. Reconnect through the chooser after removal.

For standard Bluetooth LE MIDI, enable Bluetooth, put the controller into its Bluetooth MIDI mode, and choose **Connect Bluetooth LE MIDI**. Grant Nearby devices permission on Android 12+, or location permission on older Android releases. Older phones may also require their location service enabled for BLE discovery. The scan lasts ten seconds and looks for the standard BLE MIDI service. Pairing in Android settings is not required for the app's BLE MIDI connection flow.

Routing options:

| Setting | Behavior |
| --- | --- |
| Original channels | Channels 1–3 play synth tracks, channel 10 plays drums by default; other channels play the selected track |
| Selected track | Rewrite channel messages to channel 4, which normally follows the selected track |
| Drums | Rewrite channel messages to channel 10; keep the SLOOP drum channel at its default 10 |
| First pad note = -1 | Preserve General MIDI note numbers |
| First pad note = 36, or another number | In Drums routing, map sixteen consecutive pad notes to SLOOP's sixteen drum lanes |

The pad mapping preserves velocity and note-off messages. CC messages use the firmware's existing parameter map. MIDI clock and start/continue/stop enter the USB clock path: set GLO → SYSTEM → SYNC to USB to follow an external clock. Running status, split messages, realtime interleaving, and per-port parser state are supported. Disconnect, permission/lifecycle changes, and queue overflow trigger an all-notes-off safeguard. MIDI output, arbitrary SysEx editing, MIDI learn, and proprietary wireless protocols are outside this initial implementation.

### Context-sensitive MIDI knobs

Choose **MIDI knobs → FM-1** in the top menu to make seven controller CCs turn the original UI encoders. The mapping is off by default. Configure the controller to send seven consecutive CC numbers; with the default first CC of 20:

| CC | FM-1 encoder |
| --- | --- |
| 20 | SELECT |
| 21 | ALGO |
| 22 | PRESETS |
| 23–26 | K1–K4 |

These are physical encoder movements: K1–K4 follow the current page, SELECT changes tempo or navigates as on FM-1, and ALGO/PRESETS retain their original behavior. Mapped CCs are consumed before MIDI channel routing and do not also invoke the fixed parameter CC map. Other messages keep their existing behavior. The optional channel filter uses the controller's original channel; **Any MIDI channel** accepts the configured CCs on all channels. Mapping settings persist across app restarts.

Select the message format that your controller sends:

- **Absolute 0–127:** differences between successive values become detents. The first value establishes a baseline without changing anything; repeated values do nothing. Baselines are separate for each input port, channel, and knob, and reset on reconnection, MIDI flush, or mapping changes. Absolute controls can run out of movement at 0/127; there is no wrap detection or parameter-position synchronization.
- **Relative 1 up / 127 down:** two's-complement deltas; 0 and 64 are treated as neutral.
- **Relative 65 up / 63 down:** offsets from 64; 64 is neutral.
- **Relative 1 up / 65 down:** sign/magnitude deltas; 0 and 64 are neutral.

Relative mode is preferred for endless controllers because there is no position to match when changing pages. Free rotation alone does not establish which MIDI format a controller uses. The JP-1's actual output must be checked when available. This feature maps knobs only; controller button/step-key emulation remains future work.

The M-Vave SMC-PAD/SK12 is a target for future physical testing. Its manual identifies SK12 and USB-C connectivity; standard USB MIDI and BLE MIDI are the implemented paths. Compatibility with every SMC variant or firmware revision is not established. A proprietary wireless receiver may work if it presents class-compliant USB MIDI; USB-C alone does not identify its protocol. See the [controller manual](https://fccid.io/2ARCP-SMCPAD/User-Manual/User-manual-7877306.pdf), [Android MidiManager](https://developer.android.com/reference/android/media/midi/MidiManager), and [Bluetooth permissions](https://developer.android.com/develop/connectivity/bluetooth/bt-permissions).

## Replay the additions

Export the complete port as an additive patch set:

```sh
python3 ports/android/replay/export.py /tmp/sloop-android-patches
```

The output contains `0001-add-android-port.patch`, `series`, `manifest.json`, and `apply.py`. The manifest records the source commit, patch checksum, and upstream input checksums. It excludes downloaded tools, APKs, generated assets, signing keys, and build artifacts. It also excludes unrelated working-tree changes and the planning documents.

Apply it to a matching checkout:

```sh
python3 /tmp/sloop-android-patches/apply.py /path/to/sloop-fm1
```

The helper checks upstream inputs and `git apply --check` before writing additions. It refuses an incompatible source tree or an already-applied patch. To review a newer upstream version, inspect the changed inputs, adjust the checked overlay replacements if needed, run tests, and export a new patch set. Do not bypass a mismatch blindly.

For manual application after compatibility review:

```sh
git apply --check /tmp/sloop-android-patches/0001-add-android-port.patch
git apply /tmp/sloop-android-patches/0001-add-android-port.patch
```

Copying `ports/android/` out of the repository is also supported through `SLOOP_SOURCE`. The two approaches preserve the upstream firmware files.

## Verification and current limits

```sh
python3 ports/android/tests/run.py
```

Host checks cover arbitrary render chunk sizes, all engines, MIDI channel routing, transport stop, project persistence/restore, ARM64 user sample offset handling, malformed sample rejection, and the real UI framebuffer. Java tests cover the MIDI parser and encoder message formats, filtering, baselines, and source isolation; core tests verify that encoder events change the current ENV/FX page's parameter. Existing project migration, sequencer, and UI/fuzz tests also run against the generated assets without an FM-1 build.

Device smoke test on 2026-10-10: installed and launched on a Pixel 9 Pro running Android 17. Touch keys produced clear sound confirmed by the user; transport and encoder controls responded, and a tempo change survived normal suspension and a cold restart. The output negotiated 48 kHz. Increasing the requested buffer from two to six hardware bursts eliminated observed underruns during the subsequent brief idle/touch test. System-bar and display-cutout insets keep controls accessible on this device. These checks do not establish measured latency, sustained heavy-pattern performance, or USB/BLE controller compatibility.

The engine stays at 44.1 kHz. The output adapter uses streaming linear interpolation at the device's negotiated rate. This preserves engine timing but is an initial quality compromise; a higher-quality FIR resampler and callback-driven backend can follow measured device results. Blocking writes, UI work, storage flushes, and Android scheduling mean low-latency flags are not a latency guarantee.

Before relying on the port live, measure input-to-output latency on a real phone, run dense FM6/PHYS/GRAIN patterns with effects and UI activity, exercise audio route changes and interruptions, and test USB/BLE pad disconnects and external clock. The APK build and host checks cannot establish these hardware results.

Code remains GPL-3.0-only. The APK includes the repository licensing and asset notices; distribute matching source and generated-port instructions with any published build. See [LICENSING.md](../../LICENSING.md).
