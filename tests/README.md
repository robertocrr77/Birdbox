# V2 validation

`light_policy_test.c` exercises the actual C light policy without ESP-IDF or
hardware. On Windows, build with a host-capable Clang/LLD (the installed
Espressif esp-clang includes these):

```powershell
clang --target=x86_64-pc-windows-msvc -ffreestanding -nostdlib -fuse-ld=lld -O1 '-Wl,/entry:mainCRTStartup' '-Wl,/subsystem:console' tests/light_policy_test.c -o build/light_policy_test.exe
./build/light_policy_test.exe
$LASTEXITCODE
```

Exit 0 is success; another value is the failed source line. This does not
exercise ADC hardware, actual sleep/wake, MP3 playback or power sequencing.

To verify schematic and PCB net agreement, first export with KiCad 10:

```powershell
kicad-cli sch export netlist resources/schematics/schematics.kicad_sch --format kicadxml -o build/v2-light.net
& 'C:/Program Files/KiCad/10.0/bin/python.exe' tests/verify_v2_nets.py
kicad-cli sch erc resources/schematics/schematics.kicad_sch --exit-code-violations -o build/v2-light-erc.rpt
```

Build active firmware in an ESP-IDF 5.3.1 environment with `idf.py build`.
Hardware acceptance/calibration is listed in `resources/V2-Light-Sensor.md`.
