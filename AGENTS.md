# Birdbox project context

Read README.md before changing firmware or recommending hardware connections.
The goal is a sensor-triggered, battery-powered birdbox playing natural ambience
from microSD MP3 files, inspired by the linked Relaxound Birdybox. The current
firmware streams `1_mono.mp3` through `6_mono.mp3` in order from FAT32 microSD,
then returns to track 1, with a three-second pause between tracks. Failed tracks
are logged and skipped after five seconds. Sensor-triggered playback is now implemented; see current-mode notes below.
The six MP3 assets are in `sdcard/`; the user copies them to the card root.
The user confirmed SD MP3 playback works on hardware. Volume is currently 40%.
The mono MP3 sounds natural on PC. On-device minimp3 diagnostics showed ~35 ms
decoding for 24 ms audio frames and ~40 ms total preparation/pacing, causing
sustained starvation. Playback now uses Helix fixed-point MP3 decoding with
batched SD reads and no per-frame delay. User confirmed much better sound;
Helix measured ~2.3 ms per 24 ms audio frame. Occasional SD stalls (~37-39 ms)
exceeded the old 30 ms DMA capacity. DMA is now 12 x 480 frames (120 ms at
48 kHz). Hardware retest on track 1 showed ~209 frames per five seconds,
~2.3 ms decode time, ~41 ms maximum feed gaps, and zero gaps exceeding DMA
capacity across ~40 seconds. This supports real-time throughput; the metric
is not a direct underrun counter and all six tracks still need full validation.

Use the exact Waveshare ESP32-C6-Zero Pinout Definition linked in README.md.
Verify exposed board pins, not merely chip GPIO capabilities. Never invent
missing hardware details or present unverified wiring as verified. Ask only for
details that cannot be recovered from the project files or available context.
Keep confirmed wiring and project status documented in README.md.

Current workspace: C:\Users\Familie_Ruvalcaba\Documents\Projects\Birdbox (not BirdboxVS).
Current mode: ENABLE_AUDIO_PLAYBACK=1, VOLUME_PERCENT=20. GPIO22 rising edges start one full track while ready. Tracks advance 1 through 6 then 1, only on accepted triggers. Startup does not autoplay. Busy/cooldown triggers are logged and ignored, not queued. Cooldown is 3 seconds after success, 5 after failure. ENABLE_AUDIO_PLAYBACK=0 retains sensor-only monitoring. Hardware retest pending.

Decoder log control: `ENABLE_DECODER_DEBUG` in `main/main.c` is currently 0.
Set it to 1 to enable audio_diag timing/startup and MP3 format printouts.
Sensor events, operational playback messages, warnings, and errors remain visible.
Rebuild and flash after changing this flag; playback itself is unaffected.
