# V2 light sensing and sleep — 2026-11-17

Implemented in the schematic, unrouted PCB and firmware. Not flashed or tested
on hardware. Active firmware marker is `FW=birdbox-v2-light`.
The old USB prototype wiring is incompatible: PIR moves from GPIO22 to GPIO0;
GPIO22 now controls Q2 and the peripheral power switch.

## Added circuit

```text
GPIO14 ---- R5 47k ----+---- GPIO4 / ADC1_CH4
                      |
                      +---- C1 100n ---- GND
                      |
                      +---- J6 pin 1 ---- bare LDR ---- J6 pin 2 ---- GND
```

- R5: 47 kOhm, preferably 1%, axial through-hole, 7.62 mm footprint pitch.
- C1: 100 nF ceramic, through-hole, 5 mm pitch reference footprint. Compare
  actual capacitor dimensions to the selected footprint before fabrication.
- J6: two-pin 2.54 mm header for the cabled bare photoresistor. The LDR has
  no polarity. R6 represents that external LDR and is excluded from the PCB.
  This avoids assigning an unknown sensor body/lead-spacing footprint and
  allows its placement where it sees room light outside the opaque housing.
- GPIO4 and GPIO14 are exposed on the exact Waveshare board pinout. GPIO4
  is also a reset strapping/JTAG pin; this design uses SPI SD and USB flashing,
  not SDIO/external JTAG. Check reset/boot in both bright and dark conditions.
- GPIO14 is HIGH while awake, LOW and held during sleep. The divider is
  independent of `+3V3_PERIPH`, so night checks never need to power SD/audio.
  Excitation remains on during playback to avoid repeated settling delays
  interrupting the audio feed. At wake, allow 30 ms for C1 to settle.
- Disconnect all power before wiring. Use only the regulated 3.3 V system.

## Thresholds and calibration

The user measured about 7 kOhm with lights on and 88 kOhm with hallway
illumination; both should permit playback. Agreed night entry is above
100 kOhm for three seconds, daylight return below 90 kOhm.

With this divider, `Vadc = Vexcitation * Rldr / (47000 + Rldr)`.
Nominal values at 3.300 V are:

| LDR | ADC voltage | Meaning |
| --- | --- | --- |
| 7 kOhm | 428 mV | Normal bright condition |
| 88 kOhm | 2151 mV | Hallway illumination; allowed |
| 90 kOhm | 2168 mV | Return to day below this voltage |
| 100 kOhm | 2245 mV | Confirm night above this voltage |

These are calculated starting values, not calibrated resistance measurements.
ADC curve-fitting calibration converts readings to mV, but GPIO HIGH voltage,
the actual 47k value, sensor placement, ADC error and component tolerances
still affect thresholds. The 88k reading is only about 17 mV below the return
threshold: validate repeatability and adjust after assembly. Log readings via
the `light` tag, then tune `LIGHT_NIGHT_MV` and `LIGHT_DAY_MV` in
`main/light_policy.h`. Firmware averages eight ADC readings per measurement.

An open LDR/cable produces a high reading and inhibits playback. A shorted LDR
can look like bright light; this circuit does not diagnose every sensor fault.
ADC read failures inhibit new playback, fade out active playback, and retry
with timer-only wake. Invalid readings reset the dark confirmation interval.

## Runtime behavior

- Cold reset starts track 1, stays silent for two seconds and never autoplays.
- Daytime idle uses deep sleep with a 30-second light-check timer and PIR
  EXT1 wake on GPIO0. If PIR is already HIGH, wake on LOW first to rearm;
  only a subsequent armed HIGH wake can consume a track.
- Every motion wake checks light before enabling SD/audio. New darkness
  must persist for three seconds. Night uses only the 30-second timer;
  PIR wake is disabled, but the PIR remains powered to avoid warm-up cycles.
- When day returns, wait for a new motion event. A HIGH PIR at that time
  must return LOW before a later HIGH can trigger playback.
- Idle darkness is detected at the next timer check, potentially about
  30 seconds later plus confirmation. During startup, playback and cooldown,
  readings occur approximately every 50 ms (subject to SD/decoder blocking).
- Confirmed darkness starts an approximately 200 ms audio fade. Close the
  file, drain DMA with silence, disable/delete I2S, unmount SD/free SPI, float
  interface pins without pull-ups, and turn the peripheral rail OFF.
- Retain the next-track index in RTC memory across deep sleep; cold reset
  returns to 1. A consumed motion advances once even if its track fails or
  is interrupted by darkness. No busy/startup/cooldown triggers are queued.
- Success cooldown remains three seconds; failure cooldown five seconds,
  including the one-second DMA-silence drain when I2S is running. The switched
  supply is turned off during the remaining cooldown. Mount failure now
  consumes the accepted trigger and retries initialization on the next motion.
- Six MP3 files, Helix decoding, mono mixing, 40% amplitude ceiling and
  12 x 480 DMA frames are retained. Volume smoothing/averaging remains in the
  single controller task, avoiding concurrent ADC/shutdown access.
- `ENABLE_AUDIO_PLAYBACK=0` inhibits audio and uses the same light/sleep path;
  it no longer runs the V1 continuously logging sensor-only task.

## Validation and remaining work

ESP-IDF 5.3.1 ESP32-C6 build passed. Host C tests passed for the bright/dark
thresholds, strict boundaries, three-second confirmation, shadow rejection,
invalid readings, hysteresis and retained night state with fresh wake uptime.
KiCad ERC passed with zero errors/warnings. Exported light/wake/control/Q2
nets match PCB pads. The light circuit drawing was rendered for visual review.
These software checks do not establish physical operation or battery runtime.

The board remains unrouted and new footprints are only staged. Existing
module identity/footprint and enclosure-clearance issues remain unresolved.
Validate startup rail settling (initial firmware delay 200 ms), all six tracks,
sensor rearming, ADC calibration, daytime/nighttime transitions, fade/stop,
SD repeated power cycling, lack of back-power, GPIO4/GPIO5 boot straps,
and actual sleep current before accepting hardware. Firmware cannot prevent
back-power through module-internal pull-ups connected to an always-on rail;
SD pull-ups must use `+3V3_PERIPH`.

Do not flash this V2 image onto the unchanged V1 wiring. The original V1 source
is retained as `main/main-v1.c.reference` and is not compiled by CMake.

References: [Waveshare pinout](https://docs.waveshare.com/ESP32-C6-Zero#pinout-definition),
[ESP-IDF 5.3.1 sleep/wake](https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32c6/api-reference/system/sleep_modes.html).
