# Birdbox

DIY sensor-triggered ambience player inspired by the [Relaxound Birdybox](https://www.relaxound.com/birdybox-steamed-oak). An ESP32-C6 reads natural birdsong/forest ambience MP3 files from microSD and sends audio to a small speaker through an I2S amplifier. The intended finished device is battery powered; the current prototype is USB powered.

This README consolidates the project history, wiring, firmware behavior, setup, diagnostics, and remaining work. Current configuration below is taken from the saved [main/main.c](main/main.c), reviewed on 2026-11-10. Build results and physical tests are identified separately.

## Current status

- Physical microSD MP3 playback has been confirmed by the user.
- Replacing minimp3 with Helix substantially improved sound, confirmed by the user. Timing tests support real-time playback on track 1 with the enlarged DMA buffer.
- Sensor-triggered playback and GPIO5 potentiometer volume control are implemented. Full integrated hardware verification remains pending.
- Startup waits for motion; it does not autoplay. One accepted trigger plays one complete track.
- Six tracks advance in order, then wrap to track 1. Triggers during playback/cooldown are ignored, never queued.
- The knob controls mute through a **40% digital amplitude ceiling**. This is not a percentage of speaker wattage or perceived loudness.
- Battery supply, charging, low-battery handling, and deep sleep are **not implemented**. Waiting for motion does not mean the ESP32 is in a low-power sleep state.

## Hardware needed

| Item | Specification / role | Status or limitation |
| --- | --- | --- |
| Microcontroller | Waveshare ESP32-C6-Zero | Project board; target `esp32c6` |
| Audio amplifier | MAX98357A I2S breakout | Current wiring powers VIN from 3V3 |
| Speaker | 4-ohm, 3 W | Connect only across amplifier speaker outputs |
| Storage interface | SPI microSD module with 3V3, CS, MOSI, CLK, MISO, GND | Existing module uses a 3.3 V supply; do not substitute an unverified module pinout |
| Storage | FAT32 microSD card | Holds all six MP3 files in its root |
| Motion sensor | Existing sensor referred to as PIR in firmware | Exact model and terminal arrangement are not documented; recorded connections are below |
| Volume control | 10 kOhm potentiometer | User-specified resistance; identify the actual wiper before connecting |
| Prototype supply/programming | USB data cable and computer running ESP-IDF | Current USB-powered setup |
| Interconnects | Suitable jumper wires and prototype connections | Keep SPI/audio wiring short and secure; share ground |
| Future power system | Battery, charger/protection, and suitable supply circuitry | Models, voltage conversion, connections, and runtime budget remain undecided |
| Final assembly | Enclosure and secure mounting | Final mechanical design is not documented |

Do not assume the DIY sensor behaves like the reference product's light-change detector. The reference product and its short sound duration are inspiration; this firmware plays each supplied file in full.

## Wiring and power

Disconnect USB and any other power before changing connections. Use the board's **GPIO labels**, not header-position numbers. Board reference: Waveshare's exact [ESP32-C6-Zero Pinout Definition](https://docs.waveshare.com/ESP32-C6-Zero?variant=ESP32-C6-Zero#pinout-definition). The project records the GPIOs below as exposed board pins. Recheck that diagram before adapting the wiring to a different board; chip GPIO capability alone does not establish that a pin is exposed.

All modules share GND. The tables describe the recorded prototype connections, not a battery wiring design. Never apply 5 V to the SD module's 3V3 pin, potentiometer wiper, or ESP32 GPIO inputs. Verify the exact sensor's supply requirements and 3.3 V-compatible output before replacing it.

### Complete GPIO allocation

| ESP32-C6-Zero connection | Destination | Function |
| --- | --- | --- |
| GPIO1 | MAX98357A DIN | I2S audio data |
| GPIO2 | MAX98357A BCLK | I2S bit clock |
| GPIO3 | MAX98357A LRC / LRCLK / WS | I2S word select |
| GPIO5 | Potentiometer wiper | ADC1 channel 5, volume input |
| GPIO18 | SD CS | SPI chip select |
| GPIO19 | SD MOSI | SPI data to card |
| GPIO20 | SD CLK | SPI clock |
| GPIO21 | SD MISO | SPI data from card |
| GPIO22 | Sensor OUT | Motion input, rising edge |
| 3V3 | Amplifier VIN, SD 3V3, recorded sensor VCC, one potentiometer end | Prototype 3.3 V rail |
| GND | Amplifier GND, SD GND, sensor GND, other potentiometer end | Common ground |

### MAX98357A and speaker

| Amplifier terminal | Connection |
| --- | --- |
| VIN | ESP32 3V3 |
| GND | ESP32 GND |
| DIN | GPIO1 |
| BCLK | GPIO2 |
| LRC / LRCLK / WS | GPIO3 |
| Speaker + and speaker - | The two speaker wires, one to each output |
| GAIN | Left unconnected in the prototype |
| SD / EN, if present | Must not be held LOW; check the actual breakout's enable arrangement |

Neither speaker terminal connects to GND: these are differential amplifier outputs. Typical MAX98357A breakouts can operate with SD/EN unconnected, but the exact breakout circuitry has not been documented here. No MCLK connection is used.

### SPI microSD module

| Module terminal | ESP32 connection |
| --- | --- |
| 3V3 | 3V3 only |
| CS | GPIO18 |
| MOSI | GPIO19 |
| CLK | GPIO20 |
| MISO | GPIO21 |
| GND | GND |

Firmware limits SPI to 4 MHz for this jumper-wire prototype. Card mounting never automatically formats the card. Required pull-ups depend on the actual module; inspect its circuitry if mounting fails.

### Motion sensor

Recorded wiring: **VCC -> 3V3, GND -> GND, OUT -> GPIO22**. Its exact model, physical pin order, warm-up time, sensitivity, hold time, and retrigger settings are unknown. Identify terminals from the actual sensor rather than assuming a layout from a photograph.

GPIO22 is configured as an input with neither internal pull-up nor pull-down. The sensor must provide a defined, compatible logic signal. Firmware polls every 20 ms; this is not an interrupt input and very short pulses may be missed.

### 10 kOhm volume potentiometer

Connect the **verified wiper to GPIO5**, and the two resistance-track ends to **3V3 and GND**. Swap the ends to reverse rotation. The earlier stock photo did not establish the actual terminal arrangement; do not infer it here. With power disconnected, the end-to-end resistance should stay approximately constant while resistance from the wiper to either end changes as the shaft turns.

GPIO0 was not a previously implemented volume input. Use GPIO5 / ADC1 channel 5.

GPIO5 also serves as a reset-time SDIO clock-edge strapping pin and external JTAG pin. This project uses SPI storage on GPIO18-21 and USB/serial flashing, not SDIO or external JTAG. **Boot/reset must still be tested at both knob endpoints.** Physical knob behavior has not been established by a firmware build.

## Prepare the microSD card

1. Back up existing card contents before any formatting. Use FAT32; firmware does not format cards for you.
2. Copy the six files from [sdcard/](sdcard/) directly to the card root:

   ```text
   1_mono.mp3
   2_mono.mp3
   3_mono.mp3
   4_mono.mp3
   5_mono.mp3
   6_mono.mp3
   ```

   For example, `E:\1_mono.mp3`, not `E:\sdcard\1_mono.mp3`.
3. Safely eject the card and insert it with the prototype powered off.
4. Connect power, flash the firmware if needed, and check the serial monitor for successful mounting.

The recordings were converted to mono; requested ambience includes birdsong, a nearby stream, and background insects. Replacement files must be actual MPEG Layer III MP3 files with the same names. Renaming a WAV file does not convert it. The implementation accepts mono/stereo decoded frames at 8-48 kHz with 16-bit output; stereo is averaged to mono and duplicated into both I2S channels. Free-bitrate MP3 files are rejected. Files play for their own duration, with no fixed 20-second cutoff.

The internal mount path is `/sdcard`; that directory name does not belong on the physical card. Supplied assets total approximately 11.7 MB. Audio-content redistribution rights are not established by the decoder license.

## Firmware behavior

### Startup and trigger sequence

1. Startup identifies the firmware as `FW=pot-gpio5-v1`, with build date/time, decoder-debug setting, and maximum volume.
2. Volume monitoring and GPIO22 sensor monitoring start. Digital gain initially stays muted until a successful ADC read.
3. With audio enabled, firmware mounts the SD card, initializes I2S, and writes two seconds of silence before announcing `Ready for motion; next track 1/6`.
4. A newly observed LOW-to-HIGH transition while ready consumes the readiness token and starts one complete track.
5. After success, firmware writes three seconds of silence. After failure, it logs the failed file and writes five seconds of silence. This also flushes queued audio.
6. It advances the playlist index and waits for another new rising edge. Track 6 wraps to track 1; reset restarts the index at track 1.

A held HIGH does not retrigger. Initial HIGH at sensor initialization is not itself counted as an edge. Motion while initializing, playing, or cooling down is logged and discarded. It does not restart a file or queue another. Startup sensor settling may still create subsequent rising edges.

A failed file consumes that accepted trigger and advances the index after the failure cooldown; the next file requires a new trigger. If SD mounting fails, the player task stops and sensor monitoring continues without playback readiness. Correct the card problem and reset to retry.

### Knob processing

Every 50 ms the volume task averages eight 12-bit ADC readings, then smooths the result using `(3 * previous + new) / 4`. ADC attenuation is configured as `ADC_ATTEN_DB_12`. Filtered readings at or below 40 map to mute; readings at or above 4000 map to the configured maximum. Intermediate readings map linearly to digital gain.

Playback ramps from the previous gain to the new gain across each decoded frame to reduce clicks. Each track begins with its previous gain set to zero. An ADC read failure retains the last gain and logs a warning; failure before any successful read leaves gain muted.

### Configuration in main/main.c

| Setting | Saved value | Purpose |
| --- | --- | --- |
| `ENABLE_AUDIO_PLAYBACK` | `1` | Motion-triggered playback; `0` retains sensor monitoring and the volume task without starting the player |
| `VOLUME_PERCENT` | `40` | Maximum digital amplitude permitted by the knob, valid range 0-100 |
| `ENABLE_DECODER_DEBUG` | `0` | Enables decoder startup/MP3 format and timing messages when set to 1 |
| `ENABLE_VOLUME_DEBUG` | `0` | Enables GPIO5 raw, filtered, and gain logging approximately once per second when set to 1 |
| `PIR_POLL_MS` | `20` | Sensor polling interval |
| `VOLUME_POLL_MS` | `50` | Volume update interval |
| `VOLUME_ADC_MIN` / `VOLUME_ADC_MAX` | `40` / `4000` | Knob endpoint thresholds |
| `DMA_DESCRIPTORS` / `DMA_FRAMES` | `12` / `480` | I2S DMA buffer configuration |

Rebuild and flash after editing these constants. Sensor events, operational playback messages, warnings, and errors remain visible with decoder debug disabled. Older notes mentioning fixed 20% volume or `ENABLE_VOLUME_DEBUG=1` describe earlier states and are superseded by the saved values above.

## Build, flash, and monitor

Active workspace: `C:\Users\Familie_Ruvalcaba\Documents\Projects\Birdbox`, **not BirdboxVS**. Open the folder containing this README and `CMakeLists.txt`.

This is a native **ESP-IDF 5.3.1** project, target **ESP32-C6**. It does not require Arduino or Arduino Audio Tools. The bundled Helix component is built locally. Main component dependencies are ESP ADC, GPIO, I2S, SPI, FATFS, SDMMC, Helix, and ESP timer.

In VS Code with the ESP-IDF extension:

1. Select the ESP-IDF installation and run **ESP-IDF: Set Espressif Device Target** -> `esp32c6` when configuring a fresh checkout.
2. Run **ESP-IDF: Build your project**.
3. Connect a USB data cable and run **ESP-IDF: Select Port to Use**.
4. Run **ESP-IDF: Flash your project**, selecting UART/serial when asked. The board's USB Serial/JTAG connection is available through its serial COM port.
5. Run **ESP-IDF: Monitor your device**.

Alternatively, in a terminal with ESP-IDF activated:

```powershell
idf.py set-target esp32c6
idf.py build
idf.py -p COM3 flash monitor
```

Use `set-target` for initial configuration or a target change; it can regenerate build configuration. Replace `COM3` with the actual port. The saved VS Code settings currently select COM3 and UART flashing; these are machine-specific settings, not a guaranteed port assignment. Exit the monitor with **Ctrl+]**. Close other serial monitors before flashing. If download mode is needed, hold BOOT, press/release RESET, then release BOOT and retry.

`sdkconfig.defaults` sets the ESP32-C6 target and USB Serial/JTAG console. Local `sdkconfig` and generated `build/` are ignored by Git. A fresh checkout needs a build to regenerate compiler/IntelliSense artifacts.

Before saving `main/main.c`, reconcile any unsaved editor buffer with the disk version. An older buffer previously overwrote the restored potentiometer implementation. Verify the startup firmware marker and build timestamp after flashing to confirm which firmware is running.

## Expected serial output

The exact timestamps and interleaving depend on task scheduling. Typical messages include:

```text
FW=pot-gpio5-v1 built ...; decoder_debug=0; max_volume=40%
Motion playback enabled=1; volume=40%
10k potentiometer on GPIO5 (ADC1_CH5); range mute to 40%
Monitoring GPIO22; initial level=...
SD: CS=18 MOSI=19 CLK=20 MISO=21
SD mounted (card contents are never auto-formatted)
Audio: DIN=1 BCLK=2 LRC=3. Preparing motion-triggered playback.
Ready for motion; next track 1/6
Motion detected: trigger #1 (GPIO22 LOW -> HIGH)
Motion accepted: track 1/6
Playing /sdcard/1_mono.mp3 (potentiometer volume; maximum 40%)
Playback finished (... frames)
Ready for motion; next track 2/6
```

Busy events produce `Trigger ignored: audio is busy, cooling down, or not ready`. With decoder debugging disabled, absence of MP3 format or `audio_diag` lines is expected. With volume debugging disabled, absence of periodic raw knob readings is expected.

## Audio repair history and measured performance

The original mono MP3 sounded natural on the PC but metallic/robotic on the ESP32-C6. On-device minimp3 measurements showed roughly 34.3-34.9 ms decoding for frames containing only 24 ms of audio, and about 40 ms total preparation/pacing. Sustained starvation explained the symptom; it was not sufficient evidence of an amplifier defect.

The repair replaced minimp3 with **Helix fixed-point MP3 decoding**, batched compressed SD reads into a 16,384-byte input buffer, and removed the artificial delay after each frame. Blocking I2S writes now pace playback. The user confirmed much better sound. Helix measured approximately **2.3 ms decoding per 24 ms audio frame**, about 15 times faster than the measured previous decoder.

Occasional SD reads of approximately 37-39 ms still exceeded the former 30 ms DMA capacity. Buffering was increased to **12 x 480 stereo frames**, consuming 23,040 bytes of 16-bit stereo sample memory and representing **120 ms at 48 kHz**. Capacity in time changes with sample rate.

A track-1 hardware retest over approximately 40 seconds showed around 209 decoded frames per five-second reporting window, about 2.3 ms mean decode time, approximately 41 ms maximum feed gaps, and zero feed gaps exceeding configured DMA capacity. This supports real-time throughput for that test; it does not establish all-track reliability or directly count I2S underruns.

### Decoder diagnostics

Set `ENABLE_DECODER_DEBUG=1`, rebuild, flash, then capture the decoder startup, MP3 format, and at least three pairs of `audio_diag` messages during playback. Reports occur about every five seconds and at track end when frames have been collected.

| Metric | Meaning |
| --- | --- |
| `audio/frame` | Audio duration represented by a decoded frame, in microseconds |
| `SD avg/max` | Compressed-read time per reporting window |
| `decode avg/max` | Decoder time per reporting window |
| `prepare+pace avg` | Preparation time before I2S write plus explicit pacing delay; blocking I2S writes are excluded, explicit delay is now zero |
| `over-budget` | Frames whose preparation/pacing took longer than their audio duration |
| `max feed gap` | Longest measured interval between a completed write and the next write starting |
| `gaps exceeding DMA capacity` | Feed gaps exceeding the maximum configured buffer duration at the active sample rate |

These timing indicators are **not hardware underrun counters**. The actual queue may be partially filled, so zero long gaps does not prove zero underruns. Logging itself adds overhead. Restore debug to 0 after diagnosis if the extra output is no longer needed.

## Troubleshooting

| Symptom / message | Checks and recovery |
| --- | --- |
| `SD mount failed` | Power off; check FAT32, 3V3, common ground, GPIO18-21 wiring, card seating, and module pull-ups. Correct the cause and reset; there is no automatic remount loop. |
| `Cannot open /sdcard/...` | Check exact numbered filename and card root. Failure advances the index after five seconds; another trigger is required. |
| `No playable MP3 frames`, decode error, unsupported or truncated MP3 | Replace/re-encode the affected file as supported Layer III MP3. Check for damaged card data. |
| Playback messages but no sound | Turn the knob up; check GPIO5 wiper/end wiring and amplifier enable, DIN/BCLK/LRC, speaker terminals, and common ground with power disconnected. |
| Knob has no effect | Temporarily enable volume debugging, rebuild/flash, and check raw/filtered changes. Confirm GPIO5 rather than GPIO0 and identify the actual wiper. |
| Knob does not reach mute or maximum | Inspect debug readings at both ends, then tune ADC thresholds only from measured values. |
| `ADC read failed` | Firmware keeps the previous gain. Check volume input wiring and ADC setup; initial gain remains mute if no read succeeds. |
| Motion logs but no playback | Check `ENABLE_AUDIO_PLAYBACK`, successful SD mounting, and `Ready for motion`. Events during busy/cooldown/startup are intentionally ignored. |
| Held sensor HIGH does not start another track | Expected: input must return LOW and then rise HIGH after readiness. |
| Unexpected startup motion | Allow the actual sensor to settle; its warm-up behavior has not been characterized. |
| Metallic, interrupted, or stuttering sound | Enable decoder diagnostics; compare preparation and feed gaps with audio duration and DMA capacity. Check card/wiring as well as decoding performance. |
| Flashing fails or old behavior remains | Confirm correct workspace, target and COM port, close competing monitors, rebuild/flash, and inspect startup marker/build time. Reconcile unsaved editor content. |
| Boot changes with knob position | Record behavior at each endpoint and investigate GPIO5's reset-time strapping role before treating the assembly as validated. |

## Work completed and validation record

| Stage | Work / evidence | Limit |
| --- | --- | --- |
| Initial audio prototype | ESP32-C6-Zero, I2S amplifier/speaker, and SPI SD wiring documented; native ESP-IDF MP3 player implemented | USB-powered prototype |
| 2026-11-01 build and desktop checks | ESP-IDF 5.3.1 ESP32-C6 build passed, with about 73% of the existing 1 MB app partition free; FFmpeg decoded a supplied MP3 without errors (32 kHz, mono, 96 kbps, about 30.06 s including padding) | Historical check of a supplied file, not metadata/validation for all six current assets |
| Physical SD playback | User confirmed playback from microSD | Does not by itself validate every later change |
| Six-track playlist | Numbered mono assets added; sequence 1-6 then 1 | Full current playlist test remains pending |
| Decoder repair | Helix replaced minimp3; 2026-11-01 build passed with about 72% app partition free; user confirmed improved sound | Historical size figures, not a new build measurement |
| DMA repair | Buffer enlarged after measured SD stalls; track-1 timing retest supported real-time throughput | Approximately 40-second sample; not all tracks or direct underrun measurement |
| Motion mode | GPIO22 rising-edge monitoring, one-track triggers, ignored busy events, and success/failure cooldown implemented; build reported passed | Sensor-to-audio hardware retest pending |
| 2026-11-08 volume control and repair | GPIO5 ADC knob, smoothing, gain ramp, 40% ceiling and firmware marker restored after saved source reverted to fixed gain/unconditional diagnostics | Physical endpoint, boot, and playback tests pending |
| 2026-11-10 documentation review | Consolidated README against saved source; both debug flags currently 0 | Documentation-only review; no new build, flash, or hardware test performed |

Obsolete Arduino sketches, unused minimp3 sources, synthetic test audio, and temporary audio tooling were removed during earlier cleanup. No firmware source is changed by this documentation update.

### Hardware acceptance checklist

- [ ] Flash the intended build and confirm firmware marker, build time, and configuration.
- [ ] Confirm startup stays silent and waits for a new sensor edge after readiness.
- [ ] Trigger all six tracks individually; confirm full playback, order, and wrap to track 1.
- [ ] Confirm held HIGH, playback-time motion, and cooldown motion do not queue/restart tracks.
- [ ] Confirm three-second success cooldown and five-second failed-file behavior with a controlled missing-file test on a backed-up card.
- [ ] Test SD mount failure and recovery by correction/reset.
- [ ] Test knob mute, full permitted volume, smooth changes during playback, and usable travel.
- [ ] Reset/boot with the knob at both endpoints and intermediate positions.
- [ ] Listen through every complete track and collect timing diagnostics if interruptions occur.
- [ ] Characterize the actual sensor's warm-up, range, output levels, and retrigger settings.

## Remaining battery and enclosure work

Select and document the exact battery, charger/protection, and any voltage regulation before assigning connections. Establish a power budget from measured idle/playback current, check supply behavior during SD/audio load, and define safe power switching and charging behavior. Runtime cannot be calculated reliably from the available information. No battery connection or charging method is approved by the existing USB wiring tables.

Decide whether low-power sleep, amplifier shutdown, or other idle-power reductions are required for the runtime goal. Then document the final enclosure, speaker mounting, sensor placement, card/USB access, and secured wiring. These are future design tasks rather than implemented features.

## Repository map and decoder provenance

| Path | Purpose |
| --- | --- |
| [main/main.c](main/main.c) | Player, sensor state handling, ADC volume, configuration, timing diagnostics |
| [main/CMakeLists.txt](main/CMakeLists.txt) | Main component dependencies |
| [components/helix/](components/helix/) | Vendored fixed-point MP3 decoder, local build integration, original licenses |
| [sdcard/](sdcard/) | Six numbered mono MP3 assets to copy to the card root |
| `.vscode/` | Local ESP-IDF and IntelliSense configuration |
| `sdkconfig.defaults` | Default target/console configuration |
| `CMakeLists.txt` | ESP-IDF project entry (`birdbox_audio_test`, retained project name) |
| `build/`, `sdkconfig` | Generated/local configuration; ignored by Git |
| [AGENTS.md](AGENTS.md) | Project context and working instructions |

Helix MP3 C sources were vendored from [pschatzmann/codec-helix](https://github.com/pschatzmann/codec-helix/tree/f6e55a307c7577c5e3dfe1bf424a61075235fa08), commit `f6e55a307c7577c5e3dfe1bf424a61075235fa08`, retrieved 2026-11-01. Original headers and `LICENSE.txt`, `RPSL.txt`, and `RCSL.txt` are retained under `components/helix/libhelix-mp3`. Local memory glue supplies malloc/free; component compilation uses `-O2` and `-fwrapv`, with the upstream RISC-V integer multiply path. Review those license files before redistribution. The component's own historical README predates the successful hardware timing retest described above.
