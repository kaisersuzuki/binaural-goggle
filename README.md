# Binaural Beat + RGB LED Goggle Controller

A DIY meditation device combining a dsPIC30F4013 microcontroller with an ESP32-WROOM-32E,
RGB LED arrays mounted in ski goggle frames, binaural beat audio output, and WiFi control
via an embedded ESP32 web server. Housed in a custom ASA FDM 3D-printed enclosure.

**License:** CC BY-NC 4.0 — personal/educational use only, not for commercial use.

---

## Hardware Overview

| Component | Part | Notes |
|-----------|------|-------|
| Microcontroller | dsPIC30F4013-20I/P | PDIP-40, socketed, DigiKey |
| WiFi module | ESP32-WROOM-32E | SMD, JLCPCB assembled |
| Headphone amp | TPA6132A2 | WQFN-17, SMD |
| Digital pot (volume) | AD5220BRZ50 | SOIC-8, SPI, 64-step × 2 |
| 3.3V LDO | LD1117AG-3.3V | SO-8 |
| 5V LDO | AMS1117-5.0 | SOT-223 |
| Battery monitor | MAX8212ESA+T | SOIC-8 |
| LED driver | NTE74HC14 (74HC14) | Schmitt inverter × 2 ICs |
| LED transistor | 2N3904S | NPN × 6 |
| Crystal | 10MHz | HC-49S |
| LEDs | RGB common-cathode | 3× per eye, 2 eyes |
| Enclosure | Custom FDM ASA, JLC3DP | 99×79×51mm body + lid |
| PCB | 2-layer FR4, JLCPCB | 84.4×62.4mm, rounded corners |

### Panel Connectors
| Connector | Part | Location | Function |
|-----------|------|----------|----------|
| DC barrel jack | 5.5×2.1mm | Rear panel | 9V power input |
| Power switch | 12mm SPST latching toggle | Rear panel | Power on/off |
| GX16-8 | Female chassis socket | Front panel | Goggle cable |
| Headphone jack | Neutrik NJ3FP6C | Front panel | Stereo TRS output |
| Status LED | 5mm panel mount | Front panel | Power indicator |

---

## PCB Pin Assignments

### dsPIC30F4013 (U1)
| Pin | Function | Connected to |
|-----|----------|-------------|
| 1 | MCLR | 10kΩ → 3.3V, ICSP |
| 6 (RB4) | Eye A — Red | HC14 → 2N3904 → LED |
| 7 (RB5) | Eye A — Green | HC14 → 2N3904 → LED |
| 8 (RB6/PGC) | Eye A — Blue | HC14 → 2N3904 → LED |
| 9 (RB7/PGD) | Eye B — Red | HC14 → 2N3904 → LED |
| 10 (RB8) | Eye B — Green | HC14 → 2N3904 → LED |
| 38 (RB9) | Eye B — Blue | HC14 → 2N3904 → LED |
| 37 (RB10) | TPA6132A2 EN | Amp enable (HIGH = on) |
| 4 (RB2) | AD5220 /CS | SPI chip select |
| 5 (RB3) | AD5220 U/D | SPI up/down |
| 24 (RF6/SCK1) | AD5220 CLK | SPI clock |
| 25 (RF3/SDO1) | AD5220 DI | SPI data |
| 27 (RF5/U2TX) | ESP32 RX | UART2 TX |
| 28 (RF4/U2RX) | ESP32 TX | UART2 RX |
| 33 (RD1/OC2) | PWM Right | Audio PWM right channel |
| 34 (RD0/OC1) | PWM Left | Audio PWM left channel |
| 13 (OSC1) | Crystal | 10MHz |
| 14 (OSC2) | Crystal | 10MHz |

> **Note:** RB6 and RB7 are shared with PGC/PGD (ICSP programming pins).
> LEDs will flash briefly during firmware programming — this is expected and harmless.

### ESP32-WROOM-32E (U7)
| GPIO | Function | Connected to |
|------|----------|-------------|
| GPIO16 (RX2) | UART2 RX | dsPIC U2TX (RF5) |
| GPIO17 (TX2) | UART2 TX | dsPIC U2RX (RF4) |
| GPIO0 | Boot mode | Voltage divider on PCB |

### Connector Headers
| Ref | Pins | Function |
|-----|------|----------|
| J1 / H1 | 2×3 (6-pin) | ICSP — dsPIC programming (PICkit 4) |
| J2 / H3 | 1×6 | ESP32 programming (GND/3V3/EN/IO0/TX0/RX0) |
| J3 / PS1 | GX16-8 | Goggle cable (A_R/A_G/A_B/B_R/B_G/B_B/5V/GND) |
| J4 / H4 | 1×3 | Headphone jack (GND/R/L → Neutrik NJ3FP6C) |
| J5 / H2 | 1×2 | Status LED (VCC/GND) |
| J6 / CN1 | 1×2 | 9V power input (VCC/GND from barrel jack) |
| J7 | — | UART debug (not populated) |

### GX16-8 Pin Map
| GX16-8 Pin | Signal |
|-----------|--------|
| 1 | Eye A — Red |
| 2 | Eye A — Green |
| 3 | Eye A — Blue |
| 4 | Eye B — Red |
| 5 | Eye B — Green |
| 6 | Eye B — Blue |
| 7 | +5V |
| 8 | GND |

---

## Repository Structure

```
binaural-goggle/
├── firmware/
│   ├── dspic/
│   │   └── src/
│   │       └── dspic_firmware.c      # dsPIC30F4013 main firmware
│   └── esp32/
│       └── src/
│           └── main.cpp              # ESP32 WiFi + web server firmware
├── hardware/
│   ├── schematic/
│   │   ├── binaural_goggle_schematic.html     # Schematic rev 0.1 (original)
│   │   ├── Schematic_AUDIO-CONTROLLER_REV1.1.pdf  # Final schematic
│   │   └── images/
│   │       ├── schematic_rev0.1.png
│   │       └── schematic_rev1.1_*.png
│   ├── pcb/
│   │   ├── Gerber_AUDIO-CONTROLLER_2026-04-19.zip
│   │   ├── PCB_AUDIO-CONTROLLER_2026-04-19.json
│   │   ├── SCH_AUDIO-CONTROLLER_2026-04-19.json
│   │   └── images/
│   │       ├── pcb_top.jpg
│   │       ├── pcb_bottom.jpg
│   │       └── pcb_3d_*.jpg
│   └── bom/
│       └── BOM_AUDIO-CONTROLLER.csv
├── enclosure/
│   ├── enclosure_body_v8_FINAL.stl
│   ├── enclosure_lid_v8_FINAL.stl
│   └── insert_drawing_v7.pdf
└── docs/
    ├── README.md                     # This file
    ├── ARCHITECTURE.md
    ├── UART_PROTOCOL.md
    └── PROGRAMMING.md
```

---

## Firmware: dsPIC30F4013

### Build Environment
- MPLAB X IDE (latest)
- XC16 compiler
- PICkit 4 programmer

### Key Parameters
| Parameter | Value |
|-----------|-------|
| Crystal | 10MHz |
| PLL | ×4 |
| Fcy | 20MHz |
| DDS sample rate | 8kHz (Timer1 ISR) |
| PWM frequency | ~256kHz (OC1/OC2) |
| UART2 baud rate | 9600 |
| Beat range | 0.5–40Hz |
| Carrier range | 50–500Hz |
| Volume steps | 0–63 (AD5220) |

### Beat Pattern (default)
| Phase | Frequency | Duration |
|-------|-----------|----------|
| Baseline | 4Hz (theta) | continuous |
| Spike | 8Hz | 40 seconds every ~8 min |
| Dip | 2Hz | 90 seconds every ~13 min |
| Ramp | smooth | 3 second transitions |

### UART Command Protocol
Commands are ASCII, newline-terminated, sent from ESP32 to dsPIC:

```
BEAT:<hz>           — set beat frequency (0.5–40.0)
CARRIER:<hz>        — set carrier frequency (50.0–500.0)
VOL:<0-63>          — set volume step
RGB_A:<r>,<g>,<b>   — set eye A color (0–255 each)
RGB_B:<r>,<g>,<b>   — set eye B color
RGB_AB:<r>,<g>,<b>  — set both eyes same color
STATUS              — returns current state string
RESET               — reset sequencer to baseline 4Hz
```

dsPIC responds with `OK:<cmd>` or `ERR:UNKNOWN`.

---

## Firmware: ESP32

### Build Environment
- VS Code + PlatformIO
- Arduino framework for ESP32

### WiFi
| Setting | Value |
|---------|-------|
| Mode | AP (Access Point) |
| SSID | BinauralGoggle |
| Password | meditate123 |
| Web UI | http://192.168.4.1 |
| OTA hostname | binaural-goggle |
| OTA password | goggle-ota |

### First Flash (via J2 programming header)
1. Connect FTDI/CP2102 adapter to J2 (GND/3V3/EN/IO0/TX0/RX0)
2. Hold IO0 low during power-up to enter boot mode
3. Flash via PlatformIO: `pio run --target upload`
4. Subsequent updates via OTA

### Production Flash Note
Once `Serial` (USB debug) is no longer needed, the firmware uses `Serial2` only
(GPIO16/17) for dsPIC communication. USB serial is kept for debug output.

---

## Power

- Input: 9V DC wall adapter, 5.5×2.1mm center-positive, 1A minimum
- Panel switch (rear): SPST latching 12mm toggle, in series with 9V input
- 3.3V rail: LD1117AG-3.3V LDO (logic, dsPIC, ESP32)
- 5V rail: AMS1117-5.0 LDO (LEDs, headphone amp)
- No battery, no protection diode required

---

## Programming the dsPIC

Connect PICkit 4 to J1 (ICSP header, 2×3 male):

| Pin | Signal |
|-----|--------|
| 1 | MCLR |
| 2 | VDD (3.3V) |
| 3 | VSS (GND) |
| 4 | PGD (RB7) |
| 5 | PGC (RB6) |
| 6 | NC |

> LEDs may flash briefly during programming due to PGC/PGD sharing RB6/RB7.

---

## 3D-Printed Enclosure

Printed by JLC3DP in ASA black FDM.

| Part | File | Dimensions |
|------|------|------------|
| Body | enclosure_body_v8_FINAL.stl | 99×79×39.5mm |
| Lid | enclosure_lid_v8_FINAL.stl | 99×79×11.5mm |
| Insert drawing | insert_drawing_v7.pdf | 4× M3×4×5 at (±38, ±27mm) |

### Panel Hole Positions (Z=24mm from bottom)
| Hole | Diameter | X position | Panel |
|------|----------|-----------|-------|
| GX16-8 | 16.2mm | X=−20 | Front |
| TRS headphone | 9.5mm | X=+22 | Front |
| Status LED | 5.2mm | X=0 | Front |
| DC barrel jack | 8.2mm | X=+20 | Rear |
| Power switch | 12.2mm | X=−20 | Rear |

---

## Hardware Still Needed (to order)

- [ ] Neutrik NJ3FP6C — 1/4" stereo TRS panel jack
- [ ] GX16-8 female chassis socket + male cable plug
- [ ] 12mm SPST latching toggle switch
- [ ] 5.5×2.1mm DC barrel jack (panel mount)
- [ ] 9V DC wall adapter, 1A, center-positive
- [ ] PICkit 4 programmer
- [ ] dsPIC30F4013-20I/P (PDIP-40, DigiKey)
- [ ] USB-UART adapter (FTDI or CP2102) for initial ESP32 flash

---

*Brad Beidler · 2026 · CC BY-NC 4.0*
