# Birdbox — phase 1: USB-powered audio test

## Project goal

Build a DIY birdbox inspired by the
[Relaxound Birdybox](https://www.relaxound.com/birdybox-steamed-oak).
The intended product plays natural bird/forest ambience from MP3 files on a
microSD card when triggered by a sensor, with battery power and charging planned.
The requested sound includes birdsong, a nearby stream, and background insects.
MP3 playback is a project requirement; synthesized tones are temporary hardware tests.

Known hardware: Waveshare ESP32-C6-Zero, MAX98357A I2S amplifier, 4-ohm 3 W
speaker, and SPI microSD module (3V3, CS, MOSI, CLK, MISO, GND).
The exact sensor, battery, and charging-module models are not recoverable from
the current conversation/files; do not assume models or assign their wiring.

The reference product plays 20-second sounds, has adjustable volume, and uses a
light-change-sensitive motion detector. These are reference-product features;
the DIY playback duration and trigger/retrigger rules still need confirmation.
The confirmed playback order is 1 through 6, then back to 1, without shuffling.

## Current implementation: SD-card MP3 playback test

Native ESP-IDF 5.3.1 project for the Waveshare ESP32-C6-Zero.
Reads six numbered MP3 files from a FAT32 microSD card, decodes them in small chunks using
the bundled Helix fixed-point decoder, and plays it through the MAX98357A I2S amplifier.
Playback now requires a new PIR rising edge while ready. Playlist order: `1_mono.mp3` through `6_mono.mp3`, then back to
`1_mono.mp3`. Each file plays in full, with a three-second pause between files,
including between tracks 6 and 1. Missing or invalid tracks are logged and
skipped after a five-second pause. Resetting starts again at track 1.
PIR motion now starts one full track while idle. Motion during playback or cooldown
is logged and ignored. Battery management is not implemented. The firmware does not depend on Arduino Audio Tools.

## Wiring

Disconnect USB before changing wires.

| ESP32-C6-Zero | MAX98357A |
| --- | --- |
| GND | GND |
| 3V3 | VIN |
| GPIO1 | DIN |
| GPIO2 | BCLK |
| GPIO3 | LRC / LRCLK / WS |

Connect the 4 ohm, 3 W speaker between the amplifier's speaker + and - terminals.
Neither speaker terminal connects to GND. Leave GAIN unconnected for this test.
On typical breakout boards, SD / EN can be left unconnected; it must not be
held LOW, which shuts the amplifier down. Check your module if there is no sound.

## SD card module to ESP32-C6-Zero pinout

The selected GPIOs are exposed and marked SPI-capable in Waveshare's
[Pinout Definition](https://docs.waveshare.com/ESP32-C6-Zero?variant=ESP32-C6-Zero#pinout-definition).
The firmware must assign the SPI signals to the GPIOs listed below.
Disconnect USB before wiring. GPIO18, GPIO19, GPIO20, and GPIO21 are consecutive
pins on the right header with the USB connector at the top and the components
facing you. Use the board's GPIO labels, not physical pin positions.

| SD module | ESP32-C6-Zero |
| --- | --- |
| 3V3 | 3V3 |
| CS | GPIO18 |
| MOSI | GPIO19 |
| CLK | GPIO20 |
| MISO | GPIO21 |
| GND | GND |

Share 3V3 and GND with the amplifier. Keep its GPIO1/2/3 wiring unchanged.
Connect the module's 3V3 pin to 3.3 V only, never to 5 V.

## Prepare the SD card

1. Use a FAT32-formatted microSD card. Back up existing files before formatting
   if formatting is needed. The firmware never formats the card automatically.
2. Copy all six files from [sdcard/](sdcard/) to the **root of the card**, keeping
   the names `1_mono.mp3`, `2_mono.mp3`, `3_mono.mp3`, `4_mono.mp3`,
   `5_mono.mp3`, and `6_mono.mp3`. For example, `E:\1_mono.mp3`.
   The firmware mounts the card at `/sdcard` internally.
3. Eject the card safely. With board power disconnected, insert the card and
   connect the module using the table above.
4. Flash the project using UART and open the ESP-IDF monitor.

The current playlist uses the user's recordings converted to mono. Replace
individual tracks with MPEG Layer III MP3 files using the same numbered names.
Mono and stereo frames are supported; stereo is mixed to mono for this speaker.

## Build and run in VS Code

Open the BirdboxVS root folder (the one containing this README and CMakeLists.txt).
Use the ESP-IDF extension, rather than the generic CMake build command.

1. Run **ESP-IDF: Set Espressif Device Target**, selecting **esp32c6**.
2. Run **ESP-IDF: Build your project**.
3. Connect the board with a USB data cable and run **ESP-IDF: Select Port to Use**.
4. Run **ESP-IDF: Flash your project**, using UART/serial if asked for a method
   (the built-in USB Serial/JTAG port can be used through its COM port).
5. Run **ESP-IDF: Monitor your device**.

Alternatively, in an ESP-IDF terminal:

```powershell
idf.py set-target esp32c6
idf.py build
idf.py -p COM6 flash monitor
```

Replace COM6 with the board's actual port. Exit the monitor with Ctrl+].
If the board will not enter download mode, hold BOOT, press and release RESET,
then release BOOT and try flashing again.

## Expected result

### Diagnose metallic or robotic playback

The mono recording sounds natural on the PC but metallic on the birdbox.
Measured minimp3 timing established a sustained decoding deficit at 48 kHz:
each frame contains 24,000 us of audio, but decoding took about 34,300-34,900 us
and preparation plus pacing took about 40,000 us. Nearly every feed gap exceeded
the 30 ms DMA capacity. SD reads averaged about 1,200 us after startup.

The firmware now uses Helix fixed-point decoding, batches compressed SD reads,
and removes the artificial per-frame task delay. Blocking I2S writes pace output.
The user confirmed much better sound. Helix diagnostics measured about 2,300 us
decoding per 24,000 us audio frame (about 15 times faster), with occasional SD
reads of 37-39 ms exceeding the old 30 ms DMA capacity, around six times per
five seconds. DMA buffering is now increased to 12 x 480 frames (120 ms at
48 kHz, 23,040 bytes of sample memory) to absorb those measured stalls.
Hardware retest on track 1 over approximately 40 seconds showed about 209 frames
per five-second reporting window, ~2.3 ms average decode time, ~41 ms maximum
feed gaps, and zero gaps exceeding DMA capacity. This supports sustained
real-time playback on the measured track; timing indicators are not direct
underrun counters and do not establish behavior for every track. Firmware emits `audio_diag`
lines every five seconds during playback and at the end of each track:

- `audio/frame`: duration represented by a decoded frame, in microseconds.
- `SD avg/max` and `decode avg/max`: time spent reading and decoding.
- `prepare+pace avg`: preparation plus the existing task delay, excluding
  blocking I2S writes. Pacing delay is now zero. Compare this with `audio/frame`
  for sustained throughput.
- `over-budget`: decoded frames whose preparation plus delay exceeded their
  audio duration. Occasional overruns can be absorbed by buffering.
- `max feed gap`: longest interval from a completed I2S write to the next write.
- `gaps exceeding DMA capacity`: intervals longer than the maximum configured
  DMA buffering (12 x 480 stereo frames, 120 ms at 48 kHz). This is a conservative
  timing indicator, **not a hardware underrun count**; zero does not prove that
  no underruns occurred, since the queue can be partially filled.

Flash via UART, open ESP-IDF Monitor, and capture the MP3 format line and at least
three pairs of `audio_diag` lines. Check that startup says `Decoder=Helix
fixed-point`. The 40% volume and six-track sequence are retained.
Logging adds some overhead. Do not infer a faulty amplifier from timing alone.

After startup, the console prints `SD: CS=18 MOSI=19 CLK=20 MISO=21`,
`SD mounted`, and the card information. After a two-second pause, it prints
`Track 1/6`, `Playing /sdcard/1_mono.mp3`, and the MP3 format. Playback advances
through tracks 2 to 6, with three-second pauses, then returns to track 1.
Audio uses 16-bit Philips I2S with identical left/right samples. The sample
rate follows each decoded file.

Volume is controlled by `VOLUME_PERCENT` in `main/main.c`, currently 40% amplitude.

- `SD mount failed`: disconnect power and check card format, wiring, 3V3,
  common ground, and the SD module's required pull-ups; reconnect/reset to retry.
  SPI is limited to 4 MHz for this prototype.
- `Cannot open /sdcard/...`: check the numbered filename and card root directory.
  The player skips the failed track after five seconds and proceeds to the next.
- `No playable MP3 frames` or `Invalid MP3 data`: replace the file with the
  affected MP3; renaming a WAV to `.mp3` does not convert it.
- Playback logs but no sound: disconnect USB and check amplifier wiring,
  speaker terminals, and amplifier enable pin.

Build and desktop MP3 decoding checks do not establish SD reliability or
real-time playback performance on the physical ESP32-C6; verify these after flashing.

Validation (2026-11-01): ESP-IDF 5.3.1 ESP32-C6 build passed; firmware fits the
existing 1 MB app partition (73% free). FFmpeg decoded the entire supplied MP3
without decoding errors: 32 kHz, mono, 96 kbps, approximately 30.06 seconds
including encoder padding. The user subsequently confirmed physical SD playback
works. The new six-track sequence needs verification after flashing.

Helix replacement build passed on 2026-11-01 (72% of the app partition free).
The decoder's original license texts and source provenance are retained in
`components/helix/`. Obsolete Arduino sketches, the unused minimp3 decoder,
synthetic audio assets, and temporary audio tooling have been removed.

## Repository contents

- `main/`: ESP-IDF player and timing diagnostics.
- `components/helix/`: required MP3 decoder, provenance, and license files.
- `sdcard/`: six numbered mono MP3 files for the card root.
- `.vscode/`: project and IntelliSense configuration.
- `sdkconfig.defaults` and `CMakeLists.txt`: build configuration.
- `README.md` and `AGENTS.md`: setup and project context.

Generated `build/` and local `sdkconfig` are ignored by Git. Keep `build/`
locally for IntelliSense; a fresh checkout must be built to regenerate it.

Next phases: verify six-track playback on hardware, add sensor activation, then battery
and charging.

## Motion-triggered playback (current mode)

Active project: `C:\Users\Familie_Ruvalcaba\Documents\Projects\Birdbox`.
`ENABLE_AUDIO_PLAYBACK=1`, `VOLUME_PERCENT=20` in `main/main.c`.
Set the enable flag to 0 for sensor-only monitoring, then build and flash.

PIR wiring: VCC=3V3, GND=GND, OUT=GPIO22. The monitor polls every 20 ms and
logs each observed LOW-to-HIGH edge. An initial HIGH is not counted as an edge.

- Startup initializes SD and audio, then logs `Ready for motion; next track 1/6`.
  It does not start music automatically.
- A new rising edge while ready plays one entire file. Each accepted trigger
  advances to the next file: 1 through 6, then back to 1.
- Motion during playback or the three-second cooldown is logged and ignored;
  it neither restarts playback nor queues tracks. A held HIGH does not retrigger.
- A failed track is logged and advances after five seconds; another new motion
  event is required to play the next track.
- Sensor monitoring continues on SD failure, but playback is not ready. Reset
  after correcting the card issue. Startup sensor settling may generate edges.

Build passed; sensor-to-audio behavior still needs verification on hardware.

Decoder log control: `ENABLE_DECODER_DEBUG` in `main/main.c` is currently 0.
Set it to 1 to enable audio_diag timing/startup and MP3 format printouts.
Sensor events, operational playback messages, warnings, and errors remain visible.
Rebuild and flash after changing this flag; playback itself is unaffected.
