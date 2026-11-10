# Birdbox project context

Read README.md before changing firmware or recommending hardware connections.
README.md contains the complete hardware inventory, recorded wiring, setup,
repair history, troubleshooting, and hardware acceptance checklist. Keep it
synchronized with firmware changes. This context was reconciled with saved
main/main.c and README.md on 2026-11-10; no new build or hardware test was
performed as part of that documentation update.

## Goal and hardware status

Build a sensor-triggered, battery-powered birdbox playing natural ambience
from microSD MP3 files, inspired by the Relaxound Birdybox linked in README.md.
The current prototype is USB powered. Battery supply, charging/protection,
low-battery handling, and deep sleep are not implemented. Waiting for motion
is not a low-power sleep mode. Exact battery and charger models, connections,
and runtime remain undecided; never invent them.

Known hardware: Waveshare ESP32-C6-Zero, MAX98357A I2S amplifier, 4-ohm 3 W
speaker, 3.3 V SPI microSD module, motion sensor referred to as PIR in firmware,
and a user-specified 10 kOhm potentiometer. The sensor model and physical pin
order are not documented. The earlier potentiometer stock photo does not
establish its terminal arrangement. Distinguish recorded connections from
physical verification and do not assume replacement modules have the same pinout.

Use the exact Waveshare ESP32-C6-Zero Pinout Definition linked in README.md.
Verify exposed board pins, not merely chip GPIO capabilities. Never invent
missing hardware details or present unverified wiring as verified. Ask only for
details that cannot be recovered from project files or available context.
Disconnect power before changing wiring. Keep confirmed wiring and project
status documented in README.md.

## Recorded wiring

- MAX98357A: VIN=3V3, GND=GND, DIN=GPIO1, BCLK=GPIO2, LRC/WS=GPIO3.
  Speaker connects across amplifier + and - outputs; neither connects to GND.
  GAIN is left unconnected. Check the actual breakout's SD/EN arrangement;
  it must not be held LOW. No MCLK is used.
- SPI microSD: 3V3=3V3, GND=GND, CS=GPIO18, MOSI=GPIO19, CLK=GPIO20,
  MISO=GPIO21. Use 3.3 V only on the module's 3V3 pin.
- Sensor: recorded VCC=3V3, GND=GND, OUT=GPIO22. Verify supply requirements,
  terminal identity, and 3.3 V-compatible output for the actual sensor.
- Potentiometer: verified wiper=GPIO5 / ADC1 channel 5, ends=3V3 and GND.
  Swap ends to reverse rotation. Use 3.3 V only. GPIO0 was not a previously
  implemented potentiometer input.
- All modules share GND. These are USB-prototype connections, not a battery
  power-system design.

GPIO5 is also a reset-time SDIO clock-edge strapping pin and external JTAG pin.
Our card uses SPI on GPIO18-21 and flashing uses USB/serial, not SDIO or external
JTAG. Boot/reset at both knob endpoints and volume changes during playback
still need physical verification. Firmware build and knob verification are
separate checks.

## Current firmware and workflow

Active workspace: C:\Users\Familie_Ruvalcaba\Documents\Projects\Birdbox
(not BirdboxVS). Native ESP-IDF 5.3.1 project targeting esp32c6, with bundled
Helix decoding; Arduino Audio Tools is not required. Use the ESP-IDF build,
flash, and monitor workflow described in README.md. Saved VS Code port COM3
is machine-specific; confirm the actual port.

Saved main/main.c settings:

- ENABLE_AUDIO_PLAYBACK=1.
- VOLUME_PERCENT=40: the knob's maximum digital amplitude ceiling, not fixed
  volume, speaker wattage, or perceived loudness percentage.
- ENABLE_DECODER_DEBUG=0 and ENABLE_VOLUME_DEBUG=0.
- PIR_POLL_MS=20; VOLUME_POLL_MS=50.
- VOLUME_ADC_MIN=40; VOLUME_ADC_MAX=4000.
- DMA_DESCRIPTORS=12; DMA_FRAMES=480.

Rebuild and flash after configuration changes. Startup identifies
FW=pot-gpio5-v1 with build timestamp, decoder_debug, and maximum volume.
Reconcile unsaved editor content before saving: an older editor buffer
previously replaced the restored knob implementation with fixed gain and
unconditional decoder logging. Check the startup marker and timestamp after
flashing rather than assuming the running firmware matches the editor.

## Playback and sensor behavior

The six assets are in sdcard/; copy 1_mono.mp3 through 6_mono.mp3 to the root
of a FAT32 microSD card. Firmware mounts it at /sdcard and never automatically
formats it. SPI is limited to 4 MHz. Mono and stereo MP3 are supported; stereo
is averaged to mono and duplicated into both 16-bit Philips I2S channels.
The sample rate follows the file. Files play in full, not for a fixed 20 seconds.

Startup does not autoplay. After SD/audio initialization and two seconds of
silence, the player announces readiness for track 1. GPIO22 LOW-to-HIGH edges
start one full track only while ready. Tracks advance 1 through 6 then 1, once
per accepted trigger. Busy/startup/cooldown triggers are logged and ignored,
not queued. Cooldown is three seconds after success, five after failure.
A failed file consumes that trigger and advances the index after cooldown;
the next file still requires a new rising edge. Reset starts the index at 1.

GPIO22 is polled every 20 ms without internal pull-up or pull-down. Initial
HIGH is not counted as an edge; held HIGH does not retrigger. Startup sensor
settling may generate subsequent edges. Sensor warm-up and retrigger settings
are not characterized. On SD mount failure, sensor monitoring continues but
playback never becomes ready; correct the card issue and reset to retry.
ENABLE_AUDIO_PLAYBACK=0 runs sensor monitoring and the volume task without
starting the SD/audio player.

## Volume and diagnostics

The volume task averages eight 12-bit ADC readings every 50 ms and smooths
changes using (3 * previous + new) / 4. Playback ramps gain across each frame.
The knob ranges from mute to VOLUME_PERCENT, currently 40% digital amplitude.
Endpoint thresholds can be tuned from measured readings. Initial volume is
mute until a successful ADC read; subsequent read failures retain prior gain.

Set ENABLE_VOLUME_DEBUG=1 temporarily to print GPIO5 raw, filtered, and gain
readings approximately once per second; the saved value is currently 0.
ENABLE_DECODER_DEBUG independently enables decoder startup, MP3 format, and
audio_diag timing messages; the saved value is currently 0. Operational
playback messages, sensor events, warnings, and errors remain visible.

## Repairs and evidence

The user confirmed SD MP3 playback works on hardware. The mono recording
sounds natural on PC. On-device minimp3 diagnostics showed about 35 ms decoding
for 24 ms audio frames and about 40 ms total preparation/pacing, causing
sustained starvation. Helix fixed-point decoding, batched SD reads, and removal
of per-frame delay improved sound, confirmed by the user. Helix measured about
2.3 ms per 24 ms audio frame.

Occasional SD stalls of about 37-39 ms exceeded the old 30 ms DMA capacity.
DMA is now 12 x 480 stereo frames: 120 ms at 48 kHz and 23,040 bytes of sample
memory. Track-1 retesting over approximately 40 seconds showed about 209 frames
per five seconds, about 2.3 ms decode time, about 41 ms maximum feed gaps, and
zero gaps exceeding DMA capacity. This supports real-time throughput for that
test. Feed-gap metrics are not direct underrun counters; zero long gaps does
not prove zero underruns. All six tracks still require complete validation.

Historical ESP-IDF builds passed for SD playback, the Helix replacement, and
motion mode. GPIO5 volume control was implemented/restored on 2026-11-08.
Do not treat historical builds or desktop decoding checks as physical
verification of later changes. Preserve decoder provenance and license files
in components/helix/.

## Remaining verification and development

Use the detailed acceptance checklist in README.md. Integrated sensor/audio
behavior, all six full tracks and wraparound, ignored busy triggers, failure
recovery, knob travel/mute/ceiling, and boot at both knob endpoints remain to
be verified on hardware. Characterize the actual sensor. Then select and
document battery/charger/protection/regulation, measure the power budget, and
complete enclosure/mechanical design. Do not claim a runtime or assign battery
wiring without those details and measurements.
