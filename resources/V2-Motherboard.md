# V2 Motherboard — schematic planning template

Updated 2026-11-15. Architecture draft, not a fabrication-ready schematic.
Active firmware is now V2 with light sensing and sleep; original V1 source is archived in main/main-v1.c.reference. See ../README.md for verified history.

## KiCad implementation update

The schematic in `schematics/schematics.kicad_sch` assigns GPIO0 to PIR OUT
and HIGH-level wake, GPIO22 to active-HIGH peripheral power control, and GPIO5
to the volume wiper. The exact Waveshare board pinout was checked for exposed
GPIO0 and GPIO22. Through-hole Q1/Q2/R1–R4 switch a shared peripheral rail.
Local symbols are in `schematics/V2Modules.kicad_sym` with project library tables.
J5 provides the cabled master-switch interface. Battery wires solder directly
to the charger and speaker wires directly to the amplifier before installation,
as clarified by the user. Added J3/J4 and amplifier output pins 8/9 were removed.
SW1 is latching. A3 page layout and named net connections
were chosen to keep the expanded drawing readable.

PIR connector pin order remains the user's 1=VCC, 2=OUT, 3=GND and needs actual
AM312 confirmation. U4 remains the user's XL63020-M symbol until the delivered
regulator identity is clarified. Its footprint is photo-estimated. Direct
battery and speaker connections bypass Motherboard copper. The user has ordered
NDP6020P parts for Q1; delivery and physical validation are pending. PCB routing
and hardware validation require follow-up work after the physical details are
resolved. V2 firmware implementation is recorded in V2-Light-Sensor.md.

## User-supplied component references

| Item | Reference | Identification status |
| --- | --- | --- |
| Speaker | https://www.amazon.de/dp/B098TN7115 | Retained project specification: 4 ohm, 3 W; exact mechanical drawing pending |
| Charger module | https://www.amazon.de/dp/B0CNGX649H | Exact circuit, protection terminals, charge current, and power-path capability unverified |
| Voltage regulator module | https://www.amazon.de/dp/B0CCS57CL5 | Topology, input/output range, current, idle consumption, and footprint unverified |
| Amplifier module | https://www.amazon.de/dp/B0HJ51F4HN | User identifies MAX98357A; breakout pin order, dimensions and SD/EN circuit unverified |
| Potentiometer | https://www.digikey.de/de/products/detail/amphenol-piher-sensing-systems/PT10MV10-103A2020-E-S/9555904 | Piher PT10MV10-103A2020-E-S; project specifies 10 kOhm; obtain exact terminal/footprint drawing |
| microSD reader | https://www.amazon.de/dp/B0DQZFBCST | Retained V1 interface: 3.3 V SPI; module circuit and footprint unverified |
| MCU board | https://docs.waveshare.com/ESP32-C6-Zero | Waveshare ESP32-C6-Zero; use exact manufacturer pinout and dimensions |
| Motion sensor | https://www.amazon.de/dp/B07RJQMD9G | User identifies retained ARCELI PIR listing; exact module marking and physical pin order unverified |
| Battery | User specifies 3700 mAh | Label/model, chemistry, voltage, protection, dimensions and connector pending |

### Battery identification from user screenshot

OCTelect **LP103395 / 103395**, rechargeable LiPo, labelled **3.7 V,
3700 mAh, 13.7 Wh**; listing describes a JST connector and a pack of two.
The retail pack count does not establish use of two cells in the device.
Connector series/pitch/polarity, actual dimensions, protection circuit, allowed
charge/discharge currents and manufacturer charge limits remain unverified.
Do not infer exact dimensions solely from the model code or image proportions.
This identifies a nominal single-cell battery; the referenced TP4056 charges
to 4.2 V, subject to confirming the cell's allowed charge endpoint/current.
That voltage is below the actual regulator's stated 4.5 V minimum: the existing
regulator is not specified for this direct battery-fed V2 arrangement.
Retain the external switch arrangement, but resolve the regulator selection.

### Additional SD reader reference

User supplied https://www.berrybase.de/micro-sd-card-reader-modul-mit-spi-schnittstelle-kompakt
on 2026-11-15. Accessible manufacturer listing: **BerryBase MSD-BO1**,
EAN 4251266708872, **3.3 V**, **SPI**, approximate overall dimensions
**45 x 24 x 8 mm**. The page provides a product photograph but no linked
dimensioned footprint drawing or circuit schematic was found. Header pitch,
pad/hole coordinates, pin order, and mounting geometry remain unverified.
This reference does not prove that the existing Amazon module is identical.
Use the dimensions for provisional space allocation only, not footprint pads.

Amazon pages could not be read by the web tool during this review. Product
references are recorded without treating reseller snippets or similar-looking
modules as verified electrical specifications.

## Suggested KiCad sheets

### Working power-switching plan — 2026-11-15

**Superseded component choice:** user requires through-hole added switching
parts. Use [V2-Through-Hole-Power-Switch.md](V2-Through-Hole-Power-Switch.md)
for the concrete PMOS/NPN prototype circuit, values, manufacturer pin tables
and unresolved low-cost sourcing limitation. TPS22918 below is historical,
not the current implementation choice. The shared switched-rail concept stays.

User reports ordering the discussed XL63070/TPS63070 module; receipt, selected
output and measurements are pending. Working design uses one shared switched
3.3 V peripheral rail to reduce parts count; this supersedes the earlier
separate SD_POWER_EN/AMP_POWER_EN suggestion unless testing requires separate
sequencing. No firmware or physical wiring has changed.

Protected charger load output -> external master switch -> regulator VIN.
Regulator 3.3 V output branches into:

- 3V3_AON: ESP32-C6-Zero and PIR, powered while master switch is ON.
- TPS22918 high-side switch -> 3V3_PERIPH: SD reader supply, amplifier VIN,
  and potentiometer high end. All grounds remain common on the protected
  system side; never bridge battery-negative around a low-side protector.

Proposed U_LOAD: TPS22918DBVR (SOT-23-6). Functional wiring: VIN=3V3_AON,
VOUT=3V3_PERIPH, GND=system GND, ON=PERIPH_EN from an ESP GPIO, with a
proposed external 100 kOhm ON-to-GND resistor for default OFF on reset and
deep sleep. Final GPIO, CT rise-time capacitor, QOD discharge resistor and
input/output decoupling values remain to be designed against capacitance,
inrush and discharge requirements. This is functional wiring, not physical
pin-number instruction or a released schematic.

Manufacturer reference: https://www.ti.com/lit/ds/symlink/tps22918.pdf
The device switches the positive rail and offers configurable rise time and
output discharge. Do not use an optocoupler or switch common ground here.

Firmware power-down contract: ramp/mute amplifier, finish queued audio, close
files/unmount card, deinitialize interfaces, remove driven HIGH signals and
always-on pull-ups into unpowered peripherals, disable switch, enter sleep.
Place SD pull-ups on 3V3_PERIPH. Check amplifier SD/GAIN pull-ups, I2S, SPI,
ADC and debug paths for back-power. Disable ADC input pulls before shutdown.
Switching potentiometer supply avoids its otherwise approximately 330 uA
continuous drain at 3.3 V / 10 kOhm.

Wake sequence: boot on PIR wake, enable peripheral rail, allow supply/card/ADC
settling, initialize card and I2S with amplifier muted, then play. Preserve
playlist index across deep sleep and handle sensor-HIGH rearming explicitly.
Keep master regulator enabled in deep sleep; its EN cannot be turned off by
the ESP without also removing the ESP's normal wake-up supply.

This design needs rail sequencing/back-power review and measured sleep,
startup and playback tests before PCB release. Deep sleep is not complete
power-off: converter, board support circuits, PIR and battery protection draw
standby current. Only the external master switch disconnects the system load.

### Budget regulator candidate — RBS18349 XL63070/TPS63070 module

User supplied https://www.roboter-bausatz.de/p/tps63070-buck-boost-modul-automatische-step-up-step-down-spannungsregelung-5v-dc
Seller identifies RBS18349, lists 2–16 V input, default 5 V output, selectable
voltage via solder pads, up to 2 A depending on voltage, EN/PS controls, and
30 x 16 mm dimensions. Reviewed price EUR 4.32 incl. VAT, shipping extra;
price and stock are time-dependent. User screenshot shows XL63070 board
marking and a bridged 5V selector, with separate 3V3 and 9V pads.

Candidate conversion: with all power disconnected, fully remove the 5V
bridge and bridge only 3V3; confirm output with a meter before connecting any
3.3 V loads. The photograph and seller's adjustable-pad description support
this interpretation, but no module schematic/pad geometry has been validated.
Chip identity remains ambiguous: title says TPS63070, description XL63070;
do not assume TI's device current/idle specifications describe this board.
Verify PS mode wiring, low-load current, output voltage at battery extremes,
startup/playback transients and thermal behavior. It is a candidate, not a
confirmed replacement or committed footprint. EN disables the entire rail;
keep the main regulator on during ESP32 deep sleep to retain PIR wake-up.

TI reference for genuine IC only: https://www.ti.com/product/TPS63070

### Documented PIR reference — Adafruit 4871

User supplied DigiKey reference:
https://www.digikey.de/de/products/detail/adafruit-industries-llc/4871/13922055
Manufacturer page accessible: https://www.adafruit.com/product/4871
Manufacturer-linked datasheet: https://cdn-shop.adafruit.com/product-files/4871/4871_C15784.pdf

Adafruit specifies 3–12 V supply, digital 3.3 V output, HIGH on motion held for
another approximately 2 seconds, and 13.8 mm lens diameter. Manufacturer pin
identification: pin 1 GND (minus marking), pin 2 OUT (O marking), pin 3 supply
(plus marking). This is not a left-to-right orientation statement: match the
markings and drawing before numbering the Motherboard footprint. Standby
current and precise pad/mounting geometry have not been established in this
review. A datasheet is available, but its drawing has not yet been extracted
into a validated KiCad footprint.

This is a documented sensor reference, not proof the retained ARCELI module
has the same physical pinout or dimensions. Do not silently replace the user's
existing sensor selection. Its HIGH output is suitable in principle for the
planned HIGH-level wake input; final GPIO allocation and hardware tests remain.

### Documented amplifier reference — Adafruit 3006 (mono)

User supplied https://www.berrybase.de/adafruit-i2s-3w-class-d-verstaerker-breakout-max98357a
(BerryBase ADA3006). Original manufacturer resources are available:

- Schematic, fabrication print and downloads: https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/downloads
- Mono breakout pin functions: https://learn.adafruit.com/adafruit-max98357-i2s-class-d-mono-amp/pinouts
- Eagle schematic/board source: https://github.com/adafruit/Adafruit-MAX98357-I2S-Amp-Breakout
- Mono 3D model: https://github.com/adafruit/Adafruit_CAD_Parts/tree/main/3006%20MAX98357

Use the mono `Adafruit MAX98357 Breakout.sch` / `.brd`, not the stereo rev B
files. This provides an authoritative basis for deriving a carrier footprint
and checking SD/GAIN circuitry; no footprint extraction has yet been performed.
Preserve upstream license/attribution if copying design files. This documents
Adafruit product 3006, not the dimensions or circuit of an unverified Amazon
clone; compare actual hardware before using its footprint for another module.

### Additional regulator reference — insufficient for the complete system

**Superseded as identification of the actual hardware:** user clarified the
RBS16401 is only a similar-looking example, not the installed regulator.
Its 100 mA rating must not be attributed to the user's module.

User supplied accessible MakerMind **RBS16401** listing:
https://www.roboter-bausatz.de/p/mini-spannungswandler-dc-1.8v-5v-auf-3.3v-fester-ausgang

Seller specifies step-up/down operation, input 1.8–5 V, fixed 3.3 V output,
**100 mA continuous / 150 mA peak**, 2.54 mm pitch and approximate module size
10.3 x 7.9 x 5.5 mm without pins. Terminals are named V1/VIN, GND and V0/VO;
physical order and pad coordinates are not established here. Converter IC,
efficiency curves and quiescent current remain undocumented in this review.

Engineering assessment: **do not use this as the sole regulator for the whole
Motherboard**. Its rated continuous output is only 0.33 W, shared by MCU,
SD and amplifier if all use that rail. It does not establish adequate playback
or transient capacity. A higher-current, low-quiescent-current supply solution
must be selected and validated. This does not establish an exact required
current or prove that the prior Amazon regulator is the same module.

### Actual regulator — user-supplied listing specifications

User clarified the installed module's listing specifies **4.5–7 V input,
3.3 V output, up to 800 mA**, board size **8.6 x 12.33 mm**, power indicator,
and approximately **1.2 V input/output headroom**. Listing wording also says
more than 1 V headroom; preserve that ambiguity rather than assuming exact
dropout performance. IC identity, schematic, thermal/current derating,
quiescent current, indicator current and footprint pad coordinates are unknown.
800 mA is a listing rating, not a measured sustainable output in this housing.
User reports the actual regulator works in the current prototype; that is
operating evidence, not a full battery-range or transient test.

For a one-cell battery charged to 4.2 V by the referenced TP4056, the maximum
battery voltage would already be below this regulator's listed 4.5 V minimum.
Consequently direct protected battery output -> switch -> this regulator is
outside its stated input range. Working from USB/prototype power does not
establish battery operation. Battery voltage/chemistry remain unconfirmed;
do not silently assume a different battery or add a boost stage. Resolve the
supply architecture before finalizing the schematic.

### Additional charger reference

User supplied accessible MakerMind **RBS15537** TP4056 USB-C module listing:
https://www.roboter-bausatz.de/p/tp4056-1a-lithium-ionen-lipo-lademodul-fuer-arduino-usb-c

Listing specifications: one-cell Li-ion/LiPo, 4.2 V charge endpoint, adjustable
charge current up to 1 A, nominal 5 V input, overall 29 x 18 x 4 mm, and
overcharge/overdischarge protection. Seller links TP4056 and DW01A datasheets
and a module product-information PDF. Exact pad coordinates and identity with
the user's Amazon board remain unverified; IC package drawings alone do not
define the module footprint.

TP4056 datasheet: https://www.roboter-bausatz.de/media/5a/76/7b/1781528841/TP4056.pdf
DW01A datasheet: https://www.roboter-bausatz.de/media/0a/88/28/1781528841/DW01A_Datasheet.pdf
Module information: https://www.roboter-bausatz.de/datasheet/RBS15537.pdf

User clarified the operating arrangement: **charging with the external power
switch OFF; playback with the switch ON and charging disconnected**. The
switch sits between the charger module's protected load output and the voltage
regulator input, leaving the charger connected to the battery when OFF. No
simultaneous charging/playback is required, so a dedicated load-sharing circuit
is not required for the requested operating procedure. This is manual
separation, not an automatic interlock if USB is connected while the switch
is ON. Exact protected output labels and switch terminals remain to be checked;
do not bypass the protection return path. The seller notes USB-A-to-C charging is reliable and
some USB-C supplies may not enable 5 V. Battery label and allowed charge
current still need confirmation before matching this charger to the 3700 mAh
battery. No physical test or final connection approval is implied.

1. POWER: battery interface, charger/protection module, external master switch,
   regulator, rail test points, and provisions for low-battery monitoring.
   Keep charger battery terminals distinct from protected load terminals until
   the actual circuit is known. Charge with the system switch OFF; playback
   with charging disconnected. Simultaneous operation is not required.
   Do not bypass battery protection.
2. MCU_SENSOR: ESP32-C6-Zero and existing PIR, powered during motion-wait sleep.
   Reserve a manufacturer-confirmed exposed RTC-capable pin for sensor HIGH
   wake-up. Existing GPIO22 allocation is V1 only. Check regulator and board
   background current as well as chip sleep current.
3. STORAGE: existing SPI SD reader behind a high-side load switch, switched
   pull-ups and decoupling. Prevent back-power through SPI when its rail is off.
4. AUDIO_VOLUME: MAX98357A behind a high-side load switch; mute during rail
   transitions. Speaker wires solder directly to amplifier outputs before
   module installation, with neither output grounded.
   Power the potentiometer only when required, using an ADC-safe voltage;
   check settling and back-power paths. Keep gain ceiling at the V1 setting
   unless explicitly changed.

Use provisional functional nets PIR_OUT, SD_POWER_EN, AMP_POWER_EN,
SD_SUPPLY_SW, AMP_SUPPLY_SW, and VOLUME_ADC. Rail voltages, switch part numbers,
GPIO allocation, component values and connector pin numbers remain open until
module verification. Load-switch enables must default OFF during reset/sleep.
No optocoupler is specified; shared-ground high-side switching is the intended
approach. Select switch ratings from actual rail/load requirements.

## Firmware contract

Implemented V2 light/sleep behavior and added GPIO4/GPIO14 circuit are documented
in [V2-Light-Sensor.md](V2-Light-Sensor.md). Darkness is an agreed exception to
full-track playback: fade, stop and consume the interrupted track. Night checks
use a 30-second timer and disable motion wake; daylight return waits for new motion.


- Cold boot remains silent and allows sensor settling before arming.
- A valid motion wake powers and initializes SD/audio, then plays one full track.
- Retain next-track index across deep sleep, wrapping 6 to 1. Cold reset starts 1.
- Keep success/failure cooldowns and discard busy-time triggers.
- Before sleep, stop audio, release SD resources, place interfaces in safe
  states, then remove peripheral power.
- A held HIGH must not cause repeated tracks. Rearm only after LOW; design
  the low-power rearming state explicitly rather than sleeping on active HIGH.

## Mechanical inputs

User confirmed the speaker is glued into the housing lid and connects to the
Motherboard by two wires. The supplied Tyenaza listing screenshot shows 4 ohm,
3 W and approximate dimensions 40 x 40 x 20 mm. These are listing dimensions,
not measured tolerances. No speaker footprint is needed on the Motherboard;
use direct wires soldered to the amplifier outputs and reserve
clearance for the lid-mounted speaker, terminals and wires in the assembly.

BirdBox-Box.3mf: outer mesh bounds 106 x 152.163 x 28 mm.
BirdBox-Lid.3mf: outer mesh bounds 106 x 152.163 x 27.5 mm.
Units in both files are metres, converted above to mm. These are not board
outline limits. Review internal walls, bosses, battery/speaker space, PIR and
pot alignment, USB/card access and antenna clearance before creating Edge.Cuts.

## Technical references

- ESP-IDF 5.3.1 C6 sleep/wake: https://docs.espressif.com/projects/esp-idf/en/v5.3.1/esp32c6/api-reference/system/sleep_modes.html
- Exact board pinout: https://docs.waveshare.com/ESP32-C6-Zero#pinout-definition
- Amplifier IC (not a substitute for breakout drawing): https://www.analog.com/en/products/max98357a.html
- Load-switch example, not a committed BOM selection: https://www.ti.com/product/TPS22918
