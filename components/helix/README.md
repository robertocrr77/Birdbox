# Helix fixed-point MP3 decoder

MP3 C sources vendored from https://github.com/pschatzmann/codec-helix
commit f6e55a307c7577c5e3dfe1bf424a61075235fa08 (retrieved 2026-11-01).
Original source headers and LICENSE.txt, RPSL.txt, RCSL.txt are retained in
libhelix-mp3. Only the MP3 core and memory interface are used, without Arduino.
Local helix_memory.c provides malloc/free; CMake enables -O2 and -fwrapv for
the fixed-point arithmetic. The upstream RISC-V integer multiply path is used.

This replaces minimp3 in the playback path after measured ESP32-C6 decoding
took about 35 ms for each 24 ms audio frame. On-device timing of this decoder
must still be verified after flashing.
