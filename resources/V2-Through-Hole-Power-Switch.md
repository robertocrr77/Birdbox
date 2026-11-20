# V2 through-hole peripheral power switch

2026-11-15. Concrete prototype circuit for KiCad entry; not hardware-tested
or released for fabrication. This supersedes the SMD TPS22918 proposal.

The circuit is now entered in `schematics/schematics.kicad_sch`. PERIPH_EN
uses GPIO22, PIR HIGH-level wake uses GPIO0, and volume remains GPIO5.
GPIO0 and GPIO22 are exposed on the exact Waveshare ESP32-C6-Zero pinout.
V2 firmware sequencing is now implemented (2026-11-17); hardware validation remains pending. Symbol pin numbers and label connectivity
are checked by KiCad export/ERC, while physical pinout and footprint validation
remain hardware acceptance work.

## Circuit and BOM

Switch the regulated positive 3.3 V rail. Keep ESP32 and PIR on 3V3_AON.
All system grounds remain connected on the protected-load side of the charger.

| Ref | Part/value | Package | Role |
| --- | --- | --- | --- |
| Q1 | onsemi/Fairchild NDP6020P | TO-220, through-hole | P-channel high-side switch, electrical reference for prototype |
| Q2 | PN2222A (onsemi pinout reference) | TO-92, through-hole | Pull Q1 gate down when enabled |
| R1 | 100 kOhm, 1/4 W | Axial | Q1 gate to source: defaults OFF |
| R2 | 10 kOhm, 1/4 W | Axial | ESP PERIPH_EN to Q2 base |
| R3 | 100 kOhm, 1/4 W | Axial | Q2 base to emitter: defaults OFF |
| R4 | 1 kOhm, 1/4 W | Axial | Q2 collector to Q1 gate |

**Sourcing limitation:** NDP6020P is obsolete. It is an electrically suitable
reference, not a good unrestricted production BOM choice. A German old-stock
listing was found, but not an inexpensive current-production German distributor
source. Do not substitute NDP6020P-VB or another manufacturer's similar number
without checking its own datasheet. GOFORD G040P02T is an active TO-220
electrical alternative specified at -2.5 V, but German sourcing and its physical
pinout have not been established here. Purchasing/production selection remains
open; this limitation must be disclosed, not hidden by the circuit drawing.

User purchased NDP6020P parts from China; delivery is pending. Q1 remains
NDP6020P. This purchase does not establish authenticity or hardware validation.

## Exact reference-part connections

| Component pin | Net / connection |
| --- | --- |
| Q1 NDP6020P pin 1 G | GATE_SW: R1, R4 |
| Q1 pin 2 D, also metal tab | 3V3_PERIPH: SD supply, amplifier VIN, pot high end |
| Q1 pin 3 S | 3V3_AON: regulator 3.3 V output |
| Q2 PN2222A pin 1 E | System GND; R3 other end |
| Q2 pin 2 B | R2 other end and R3 |
| Q2 pin 3 C | R4 other end |
| R1 other end | 3V3_AON / Q1 source |
| R2 input end | PERIPH_EN GPIO22 |

For Q1, manufacturer front-view lead order is G-D-S (1-2-3); tab is drain.
For Q2 PN2222A, pins 1-2-3 are E-B-C: left to right with the flat
marked face toward you and leads down. This replaces the BC547B C-B-E mapping.
The schematic and unrouted PCB pad nets were updated on 2026-11-17.
The user reports approximately 0.670 V for both forward base junctions and
OL reversed; this is a junction check, not a physical collector/emitter test.
The photographed part is marked PN / 2222A / 6E; manufacturer is unidentified. Match symbol pin numbers to actual manufacturer package drawing
and footprint when entering KiCad. Only Q1's listed manufacturer/version is
covered by the pin table; an alternative needs its own check.

```
3V3_AON -------+---------------- S Q1 D -------- 3V3_PERIPH
               |                   |
              R1                  GATE_SW
               |                   |
               +---- GATE_SW ------R4---- C Q2
                                         E ------ GND
PERIPH_EN ---------------- R2 ----------- B
                                         |
                                        R3
                                         |
                                        GND
```

The line above between source and drain denotes the switched Q1 channel,
not a copper bypass. Q1 source must face the always-powered rail; reversing
source/drain permits unwanted powering through its body diode.

## Logic and calculated behavior

- GPIO HIGH -> Q2 ON -> Q1 gate near ground -> peripherals ON.
- GPIO LOW/high impedance -> Q2 OFF -> R1 raises gate to source -> OFF.
- Ensure PERIPH_EN has no boot-time or sleep pull-up/held HIGH. Set LOW and
  release it appropriately before deep sleep. External R3 handles floating
  reset states but cannot override a pin actively driven HIGH.
- Gate drive is approximately -3.2 V with Q2 saturated. NDP6020P specifies
  maximum RDS(on) 0.075 Ohm even at -2.5 V drive and 25 C, unlike common
  10 V-only PMOS parts. At 1 A this gives a conservative room-temperature
  channel drop estimate of 75 mV and dissipation of 75 mW; this is a
  calculation, not a measured system current or a hot-temperature guarantee.
- Base resistor current approximately (3.3-0.7)/10k = 260 uA; gate pull-up
  path draws approximately 32 uA while ON. They do not create those continuous
  currents while OFF, apart from leakage.

## Integration and tests

Keep existing module decoupling. Final local bulk/bypass capacitor values
depend on installed modules and measured rail behavior. This discrete circuit
has no specified soft-start, current limiting, or active output discharge;
R4 is a gate resistor, not an inrush-current limiter. Check startup droop
and MOSFET turn-on behavior with actual SD/amplifier capacitance. Do not add a
large gate capacitor without reviewing switching dissipation and stability.

SD pull-ups belong on 3V3_PERIPH. Before OFF: mute/flush audio, close and
unmount SD, stop interfaces, disable pull-ups and driven HIGH signals into
unpowered modules, then set PERIPH_EN LOW. Include SPI, I2S, amplifier SD/GAIN
and ADC in the back-power review. Drain discharge is through loads/leakage;
verify the rail falls sufficiently before power-cycling SD. A discharge
resistor/circuit can be added if measured behavior requires it.

After wake, enable rail, wait for stabilization, initialize ADC/SD/I2S while
muted, then play. Keep ESP32/PIR/regulator powered during deep sleep. The
master switch remains charger protected output -> switch -> regulator input.

Acceptance: verify regulator 3.3 V unloaded first; test peripheral rail OFF
and ON; check it remains OFF at reset; measure sleep draw and back-power;
measure startup/playback voltage/current and temperature at battery extremes;
test repeated SD power cycles and PIR wake/rearm. Disconnect all power before
changing wiring. V2 firmware now controls this circuit; no flash or physical test has been performed.
See V2-Light-Sensor.md for shutdown, wake and light-sensing behavior.

## Sources

- onsemi NDP6020P manufacturer datasheet: https://www.onsemi.com/download/data-sheet/pdf/ndp6020p-d.pdf
- onsemi PN2222/PN2222A manufacturer datasheet (university-hosted): https://users.ece.utexas.edu/~valvano/Datasheets/PN2222-D.pdf
- Lifecycle evidence: https://www.digikey.com/en/products/detail/onsemi/NDP6020P/1055922
- German old-stock example, EUR 9.95 for 3, shipping extra at review: https://www.ebay.de/itm/326775934301
- Active electrical alternative: https://www.gofordsemi.com/en/parts/g040p02t.html
