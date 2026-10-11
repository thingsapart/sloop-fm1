# SLOOP on the Klickwerk ESP32-P4 hardware

This is an additive PlatformIO host for the JC4880P443C ESP32-P4 engineering revision 1.0 board and Klickwerk's Psion keyboard controller. It runs SLOOP's full four-track groovebox and original interface. No files in SLOOP's `firmware/` or in Klickwerk are edited.

The firmware builds and portable tests pass. It has been **flashed to the connected ESP32-P4 revision 1.0**. Boot logs confirm 32 MiB PSRAM, codec setup, SD mount and 800 × 480 panel/GT911 initialization. Full audio, visual, keyboard and power-loss qualification remains pending; see the hardware checks below.

## Architecture and upstream updates

The engine wrapper in `engine/core.c` composes SLOOP's existing C sources and provides the FM-1 hardware entry points for input, time, RGB565 pixels and an in-memory flash image. It exports PCM16 audio and ordinary button/key/encoder events. The project deliberately retains SLOOP's UI and persistence implementation, rather than creating a second sequencer or parameter model.

Klickwerk supplies the board initialization, common I2C bus, DSI panel/touch driver, ES8311/USB audio driver, SD mount and Psion keyboard protocol client. Its sequencer, Loom interface and project model are not linked. `src/platform.cpp` supplies the small power/touch policy hooks expected by those drivers; automatic sleep is currently disabled.

`tools/prepare.py` reads both source checkouts, verifies content against `sources.lock.json`, generates SLOOP's assets and copies its firmware into ignored `build/gen/`. The generated copy has three checked sample-address replacements, two read-only knob-label/value observation hooks before native rendering caches, and maps FM-1 `.pool` sections to IDF external BSS. These changes never touch the source checkout. A source mismatch or changed replacement site fails the build rather than silently accepting an incompatible version. The pinned Klickwerk USB compatibility scripts run against build copies of IDF components.

One task on core 1 owns all SLOOP state, including input, synthesis, UI drawing and persistence snapshots. Core 0 owns LVGL/display, keyboard polling and filesystem writes. Only commands, pixel copies, read-only control/help snapshots and LED snapshots cross these boundaries. Audio submission uses the driver queue as backpressure, allowing its two queued blocks and four I2S DMA blocks to cushion screen drawing. Audio uses 128-frame stereo PCM16 blocks at 44.1 kHz; the Klickwerk driver handles onboard mono output or USB audio conversion. SLOOP's large zero-initialized pools and flash image live in PSRAM.

This is a practical HAL around SLOOP's current source composition. There are no upstream classes to subclass. A future upstream public engine library could replace `engine/core.c` without changing the board host.

## Build with command-line tools

Requirements: Python with venv, Git, a C/C++ compiler for host tests, and these two local source checkouts. PlatformIO downloads its pinned platform, IDF and toolchain; Android Studio and the ESP-IDF GUI are unnecessary.

From the SLOOP checkout:

```sh
export SLOOP_SOURCE="$PWD"
export KLICKWERK_SOURCE="$HOME/Development/SW/AUDIO/klickwerk2"
python3 -m venv ports/esp32p4/build/pio-venv
ports/esp32p4/build/pio-venv/bin/python -m pip install -r ports/esp32p4/tools/requirements.txt
ports/esp32p4/build/pio-venv/bin/python ports/esp32p4/tests/run.py
ports/esp32p4/build/pio-venv/bin/pio run -d ports/esp32p4
```

The initial setup above was verified with Python 3.14 on macOS. The platform is pinned to pioarduino `54.03.21-2`, ESP-IDF 5.4.2 and 360 MHz. `dependencies.lock` pins the component versions, including USB UAC 1.5.0 and codec device 1.6.2. Build output is `.pio/build/sloop_p4/firmware.bin` within this project.

PlatformIO's IDF size-summary helper currently emits a `--ng` option warning with the installed size tool; the separate PlatformIO size check and firmware image generation succeed. The linker map records actual section placement. Heap usage and timing require measurement on hardware.

The partition table matches Klickwerk: 6 MiB factory application, existing NVS and 2 MiB `userfiles`. This host never formats or mounts the internal user filesystem. Upload replaces the factory application; SD saves use separate `sloop-p4-*` files. There is no OTA updater.

When ready to upload, use Klickwerk's board access lock and the actual serial port:

```sh
python3 "$KLICKWERK_SOURCE/firmware/p4/tools/device_access.py" --label sloop-upload -- \
  ports/esp32p4/build/pio-venv/bin/pio run -d ports/esp32p4 -t upload \
  --upload-port /dev/cu.YOUR_P4_PORT
```

Use the same wrapper with `pio device monitor` for serial monitoring. Close the monitor before uploading. For a bounded boot capture:

```sh
python3 "$KLICKWERK_SOURCE/firmware/p4/tools/device_access.py" --label sloop-boot -- \
  ports/esp32p4/build/pio-venv/bin/python ports/esp32p4/tools/serial_log.py \
  --port /dev/cu.YOUR_P4_PORT --seconds 20 --reset
```

## Psion controls and knobs

The 5 × 6 key matrix is mapped as follows; P01–P16 are the white piano keys from F3 through G5.

```text
FX   SEL  ENV  LFO  EDIT  GLO
P01  P02  P03  P04  HOME  PLAY
P05  P06  P07  P08  SAVE  REC
P09  P10  P11  P12  ARP   SEQ
P13  P14  P15  P16  OCT−  Fn
```

Fn + the first eleven pads plays the intervening black keys. Fn + OCT− produces OCT+. Fn + SEQ holds both octave buttons, preserving SLOOP's chord behavior. Down/up events preserve native tap and hold actions. Each held key retains its original assignment when Fn changes. Disconnect/reset releases held input and requests panic.

The physical wheel has a latched target, initially SELECT. Hold Fn and tap:

| Psion key | Wheel target |
| --- | --- |
| FX / SEL / ENV / LFO | K1 / K2 / K3 / K4 |
| EDIT | SELECT |
| GLO | PRESETS |
| HOME | ALGO |
| SAVE | MASTER |

K1–K4, SELECT, ALGO and PRESETS send SLOOP's native encoder events, so the current page determines what they adjust. MASTER changes the Klickwerk audio output level for this session. Output level and route changes are currently not saved to NVS; startup uses existing Klickwerk preferences.

The display surrounds SLOOP's original artwork with the keyboard's **L-shaped function keys**: six across the top as 5 px colour bands, and HOME/PLAY, SAVE/REC, ARP/SEQ, OCT−/Fn paired down the right. Its 240 × 240 pixels normally scale to 416 × 416 using nearest-neighbour sampling. **ZOOM** restores exact 2× scaling in a 480 × 480 area, temporarily hiding the surrounding function keys; **BACK** returns. Each collapsed top band retains an 88 × 44 touch target. A physical or touch press immediately expands that key into its full labelled button above the display; release collapses it again. Expanded buttons overlap the upper display area without resizing or moving the display. The right-hand adjusters remain available in both views. The current panel driver supplies one touch pointer; the Psion keyboard supplies simultaneous chords.

## Key guide, touch adjusters and LEDs

The interactive [workflow storyboard](design/storyboard.html) explains the beat-building, melodic recording and performance workflows behind this layout. Its drawings and example values are schematic; the firmware obtains actual parameter labels and formatted values from SLOOP's own column/dial rendering through read-only hooks in the generated source copy.

**MAP AUTO** is the initial learning mode: Fn or a held/locked musical layer brings up the 4 × 4 pad guide inside the native display area. The surrounding function keys stay in place, completing the physical 5 × 6 layout. Tap the mode button to cycle **MAP ON** (always guide), **MAP OFF** (native display), then **MAP AUTO**. Guide labels show stable pad numbers plus their current pitch, drum sound, step, effect or mix function before a press. The heading shows track, layer and Fn state. Locked layers mark their function key **LOCK**; their physical LED stays bright with the same hue.

The right-hand live help title and scrolling explanation retain the last pressed key. The guide also shows its explanation below the pads. On-screen Fn toggles a latch, including for physical-key input; other keys are momentary. Musical layers use the native hold + pad and layer + HOME lock gestures. MAP OFF and ZOOM release touch-held input while preserving physical ownership. There is no additional physical Shift key.

The eight adjusters are centred vertically, with K1–K4 in the middle rows. **Tap** selects a physical-wheel target without changing its value. **Slide vertically** after a 6 px movement threshold to send relative encoder detents (4 px/detent, preserving fractional motion). A temporary rail runs through the touch point; a marker follows the finger and a formatted value appears beside it. Input stays attached to that adjuster when dragged outside the button. Lift and drag again for more range. The rail represents relative movement, not parameter minimum/maximum.

Labels and values follow the native page and layer. Holding a SEQ step gives K1–K4 their Sound/Note, Level, Ratchet and Nudge roles, and ALGO/PRESETS become lock parameter/value. Other held layers suspend track/sound browsing. Unavailable controls are dimmed. A drag cancels when its parameter context or Fn changes; queued touch detents are checked again on the engine worker, preventing stale movement from changing a new parameter. Track-selection detents are delivered separately because the native ALGO handler consumes event direction as one track move. SLOOP's brief post-layer knob guard is preserved. Menu adjusters also retain native section/cursor/settings roles and context guards.

All on-screen physical keys, including the permanent function controls, share their LED hue through `key_color(physical_id)`: vivid blue, yellow, magenta, turquoise, orange and lime. Neighbouring colours are distinct; repeated colours elsewhere are disambiguated by position, labels and pad numbers. Held keys brighten/fill without changing hue; unused Fn pads go dark physically. The top row has six distinct landmarks.

`tests/colors_test.cpp` checks D65 CIELAB distances for every adjacent physical pair. The minimum digital sRGB delta E76 is **125.2** at full palette intensity and **55.4** across idle/active brightness combinations. These constraints do not calibrate real LED output or guarantee every form of colour vision. The keyboard controller's global LED brightness is retained.

## Saves and current limits

SLOOP's project SAVE button updates its native in-memory project slots. **SD SAVE** separately commits the complete 1 MiB persistent image, including settings, projects and user sample slots. A pending save waits for stopped transport, released controls/notes and quiet audio before the engine checkpoints and copies the image. Filesystem work then runs on the storage task. There is no automatic SD save on shutdown yet.

SD storage alternates two images with a versioned header, generation and CRC32. Startup chooses the newest valid image and falls back if it is truncated or damaged. A temporary file is flushed before replacing only the inactive copy. This preserves the previous committed image across failed writes; actual power-loss durability still depends on the card and FAT filesystem. A missing SD card allows synthesis but no persistent saving. Mounting is attempted at boot; hot-insertion/removal is not implemented.

USB **audio** is inherited from Klickwerk. USB/BLE MIDI pads, MIDI output, live microphone/USB sample recording, a sample-import UI, battery status and power management are not implemented. The engine API already accepts MIDI packets and imported samples, but no P4 transport or recording path feeds those APIs. The standalone ESP32-P4 has no wireless radio; wireless MIDI would require a companion or an external transport.

The engine/UI owner reports service durations exceeding one 128-frame audio period (about 2.90 ms). The initial on-board measurement put screen drawing near 11 ms at 25 Hz, which motivated priming the existing driver buffers. This is a conservative diagnostic, not proof of uninterrupted output. It also reports audio write failures, wait timeouts and DMA errors. These counters do not detect every underrun. Display drawing and the 1 MiB save snapshot still cost time on the engine task; qualify them under worst-case polyphony and effects before relying on the port for performance.

## Export and replay

```sh
ports/esp32p4/build/pio-venv/bin/python ports/esp32p4/tools/export.py
```

This produces `build/export/sloop-p4-host.tar.gz` and `sloop-p4-additive.patch`. The archive is a standalone project: unpack anywhere, set `SLOOP_SOURCE` and `KLICKWERK_SOURCE` to the same checked dependencies, create its local venv, then run `tests/run.py` and `pio run` there. It includes no generated code, package caches or upstream source copies.

To replay the additions into a SLOOP checkout without this port:

```sh
git apply --check /path/to/sloop-p4-additive.patch
git apply /path/to/sloop-p4-additive.patch
```

The patch only adds `ports/esp32p4/`. Upstream dependency source and licenses remain in their own checkouts; preserve those alongside this host when distributing the combined firmware. SLOOP and this engine adaptation use GPL-3.0-only; upstream components retain their notices.

For an upstream upgrade:

1. Update the SLOOP or Klickwerk checkout and review its changes. Normal builds should now reject the old lock.
2. Run the local venv's Python on `tools/prepare.py --update-lock`. Review any checked overlay failure and the lock diff. Commit identifiers are recorded for provenance; content hashes also capture local modifications.
3. Run `tests/run.py`, then `pio run`. Review dependency lock and linker map changes.
4. Repeat hardware qualification and export a new host archive/patch. Keep the previous lock and archive for rollback.

## Recorded validation

The compact-top-row update was built and flashed on 2026-10-10: **416 × 416** normal display, **5 px** resting colour bands, retained **88 × 44** touch targets, and full labels while the corresponding physical/touch key is held. The final image is **1,662,896 bytes** with 83,017 bytes of static internal RAM. Upload hashes verified; the corrected firmware ran through a 20-second reset/boot capture without crash or audio write failure, timeout or DMA error. A first build exposed synchronous LVGL style-event re-entry; the callback now responds only to input events and records the pressed state before changing styles. The standalone export builds and its additive patch applies cleanly. Physical checking of expanded key overlays and touch hit areas remains outstanding.

On 2026-10-10 the L-shaped control surface, in-place automatic key guide, native parameter labels and relative touch rails were built and flashed to `/dev/cu.usbmodem1101`. The final image is **1,661,728 bytes**, with 82,993 bytes of static internal RAM reported by PlatformIO. All programmed regions passed hash verification. A 20-second capture after reset confirmed PSRAM, codec, SD and display initialization without crash, audio write failure, timeout or DMA error. Service peaks remained around 11–12 ms. Physical confirmation of the rearranged keys, rail gestures, text at the normal scaled size and LED appearance remains outstanding.

Portable tests cover relative gesture threshold, fractional motion, reversal, capture beyond a tile, release and context cancellation; native knob labels on TRACKS/FX/held SEQ steps; discrete track selection; stale detent rejection; and menu adjustment/section guards. All existing engine, input, storage and adjacent-colour checks pass. The final exported standalone project also builds independently in `/tmp`, and its additive patch passes `git apply --check`.

On 2026-10-10 the initial port firmware was built and flashed through the shared board lock to `/dev/cu.usbmodem1101`, identified as ESP32-P4 revision 1.0. All programmed regions passed upload hash verification. That initial image is 1,627,472 bytes; the linker reports 90,665 bytes of static internal RAM and 1,497,728 bytes of external BSS, excluding task stacks, heaps and framebuffer allocations. These sizes fit the unchanged 6 MiB factory partition and the board's 32 MiB PSRAM.

A 20-second runtime capture after the buffering correction showed no crash, audio write failure, timeout or DMA error. Screen redraws continued to exceed one audio period, near 10–12 ms, so worst-case load and audible continuity remain unverified. Two startup `i2s_channel_disable` messages precede the codec driver's successful open; they did not prevent boot. Visual layout, sound, keys and durable SD saving still need physical confirmation.

The exported project built independently in `/tmp`, its portable tests passed, the additive patch passed `git apply --check`, and a simulated upstream source modification was rejected by the dependency lock. Local upload/boot/runtime/standalone-build logs are retained in ignored `build/validation/`.

## Hardware qualification

1. Boot without USB audio, check PSRAM/codec/panel initialization and confirm touch colors/orientation. Test SELECT and each K encoder on multiple pages.
2. Check all Psion pads, black notes, Fn selections, octave chord, hold actions and reconnect while holding notes. Confirm there are no stuck notes or false transport actions.
3. Listen to every engine, then test the heaviest patches with all tracks, effects and frequent UI changes. Inspect service-duration warnings, audio timeouts and glitches; tune UI rate/buffering only with measured evidence.
4. Save a project with SD SAVE, reboot and restore it. Repeat with a deliberately damaged newest image, no card and interrupted saving. Confirm existing Klickwerk SD files are unaffected.
5. Walk through the storyboard: hold SEQ + a step and drag Ratchet; release during a drag; hold FX and find REVERSE; lock GLO with HOME and unlock again; test all map modes, Fn ownership and ZOOM/BACK. Check both drag directions on every adjuster, pointer cancellation and menu settings.
6. Attach a supported USB audio device; test automatic/forced routes and hot unplug while playing. Check wheel/touch master volume.

Portable tests cover the real engine wrapper, sample addressing, native UI pixels, arbitrary PCM block sizes, context-sensitive adjustments, project restoration, keyboard ownership and actual filesystem image write/fallback behavior. They do not emulate the panel, codec or I2C hardware.
