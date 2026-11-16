# PCB rev 1.1 (AUDIO-CONTROLLER)

Designed by Darlington PCB Design, manufactured and assembled by JLCPCB, April 2026.

**Read [`docs/hardware_review_rev1.1.md`](../../docs/hardware_review_rev1.1.md) before powering a board.** Rev 1.1 has design faults that need hand rework.

| Folder / file | Contents |
|---|---|
| `bom/` | Bill of materials with LCSC part numbers |
| `cpl/` | Pick-and-place file |
| `images/` | 3D renders |
| `../schematic/PCB_PCB_AUDIO-CONTROLLER_2_2026-04-19.json` | EasyEDA PCB source |
| `../schematic/SCH_AUDIO-CONTROLLER_2026-04-19.json` | EasyEDA schematic source |
| `../schematic/Schematic_AUDIO-CONTROLLER_2026-04-19.pdf` | Schematic, 4 sheets |
| `../schematic/Gerber_...zip` | Gerbers as sent to JLCPCB |

## Main parts

| Ref | Part | Role |
|---|---|---|
| U1 | dsPIC30F4013-20E/P, PDIP-40 in a socket | Audio and LED engine |
| U3 | ESP32-S3-WROOM-1-N8R8 | WiFi and web page |
| U8 | TPA6132A2 | Headphone amplifier |
| U13, U14 | AD5220BRZ50 | Volume pots, 128 steps, up/down control |
| U2, U15 | 74HC14, DIP-14 | LED buffers (two inverters per channel) |
| Q1 to Q6 | 2N3904 (SOT-23) | LED low-side switches |
| U6 | LD1117AG-3.3 | 3.3 V regulator |
| U4 | AMS1117-5.0 | 5 V regulator |
| U5 | MAX8212 | Supply monitor. Must be disconnected, see the review |
| U12 | 10 MHz crystal | dsPIC clock |

## Headers

| Ref | Pins | Function |
|---|---|---|
| CN1 | 1 GND, 2 VIN | DC input, 7.5 to 9 V regulated |
| H1 | 1 MCLR, 2 PGD, 3 3V3, 4 PGC, 5 GND, 6 n/c | dsPIC programming (2x3) |
| H2 | 1 LED anode (via 1.2 kΩ from VIN), 2 GND | Power indicator LED |
| H3 | 1 GND, 2 3V3, 3 EN, 4 IO0, 5 TX0, 6 RX0 | ESP32-S3 programming |
| H4 | 1 GND, 2 OUTR, 3 OUTL | Headphone jack |
| H5 | 1 A_R, 2 A_G, 3 A_B, 4 +5V, 5 GND, 6 B_R, 7 B_G, 8 B_B | Goggle LEDs (cathodes switched, common anode on +5V) |
