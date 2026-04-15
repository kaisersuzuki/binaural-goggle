# PCB Files

> **Status:** Layout in progress with JLCPCB Layout Service (Atan). Gerbers will be added here once received and approved.

## What goes here

Once the PCB design is finalized, this folder should contain:

| Folder | Contents |
|---|---|
| `gerbers/` | Production Gerber files (RS-274X) — what JLCPCB needs to manufacture |
| `bom/` | Bill of materials (CSV with LCSC part numbers for each component) |
| `cpl/` | Component Placement List (CSV with X/Y/rotation for pick-and-place) |

Plus the EasyEDA Pro source files:

- `binaural_goggle.epro` — EasyEDA Pro project file
- `schematic.json` — schematic source
- `pcb.json` — PCB layout source

## Specs

- 82 × 60 mm
- 2-layer, 1.6mm thick, 1oz copper
- ENIG finish (or HASL for cheaper)
- 4× M3 mounting holes at corners (3.5mm clearance, non-plated)
- Top side components only

## Key components

- **U1** dsPIC30F4013 — PDIP-40, socketed (DIP socket on PCB, chip inserted by user, not assembled by JLCPCB)
- **U2** ESP32-WROOM-32E — SMD module, JLCPCB assembled
- **U3** TPA6132A2 (or NTE941M) — headphone amp
- **U4** AD5220 — SPI digital pot
- **U5/U6** MAX603 ×2 — 3.3V and 5V LDOs
- **U7/U8** NTE74HC14 ×2 — Schmitt inverters for LED drivers
- 6× 2N3904 — RGB LED drivers
- All passives 0603 SMD

## Programming headers

Two headers must be exposed on the board:

1. **J1 — dsPIC ICSP**: 2×3 pin, 2.54mm. Pinout: MCLR / VDD / VSS / PGD / PGC / NC
2. **JESP — ESP32 programming**: 1×6 pin, 2.54mm. Pinout: GND / 3V3 / EN / IO0 / TX0 / RX0
   Plus BOOT and RESET momentary pushbuttons (or auto-reset transistor pair)

See `docs/programming.md` for use.
