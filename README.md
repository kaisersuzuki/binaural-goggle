# Binaural Beat + RGB LED Goggle Controller

A DIY meditation device: a dsPIC30F4013 generates binaural-beat audio and drives RGB LEDs mounted in ski-goggle frames, and an ESP32-S3 serves a WiFi control page. Housed in a custom 3D-printed ASA enclosure.

**License:** CC BY-NC 4.0, personal and educational use only.

> **Status, November 2026.** PCB rev 1.1 has been manufactured. A review of the design files found faults that need hand rework before a board will run, and firmware v3.0 was rewritten to match the board as built. Start here:
>
> 1. [`docs/hardware_review_rev1.1.md`](docs/hardware_review_rev1.1.md): what is wrong and why
> 2. [`docs/bringup_checklist.md`](docs/bringup_checklist.md): what to do, in order
>
> Firmware v3.0 has been compiled (ESP32-S3) and logic-tested on a PC (dsPIC) but not yet run on hardware.

**Safety.** Flashing light can trigger seizures in people with photosensitive epilepsy, most strongly between 15 and 25 Hz. Do not use the lights if that could apply to you.

---

## Hardware (rev 1.1)

| Function | Part | Notes |
|---|---|---|
| Audio and LED engine | dsPIC30F4013-20E/P or -20I/P (U1) | PDIP-40, socketed, run at 10 MIPS on 3.3 V |
| WiFi and web page | ESP32-S3-WROOM-1-N8R8 (U3) | Assembled by JLCPCB |
| Headphone amplifier | TPA6132A2 (U8) | See review item 5 |
| Volume | AD5220BRZ50 x2 (U13, U14) | 128 steps, up/down control |
| LED buffers | 74HC14 x2 (U2, U15) | DIP-14 |
| LED switches | 2N3904 x6 (Q1 to Q6) | Low-side; LEDs are common anode on +5 V |
| 3.3 V regulator | LD1117AG-3.3 (U6) | |
| 5 V regulator | AMS1117-5.0 (U4) | |
| Crystal | 10 MHz (U12) | |
| PCB | 2-layer, JLCPCB | |

Headers, pin maps and the netlist-derived connector tables are in [`hardware/pcb/README.md`](hardware/pcb/README.md).

### Goggle connector (H5)

| H5 pin | Signal |
|---|---|
| 1, 2, 3 | Eye A red, green, blue (cathodes) |
| 4 | +5 V (all LED anodes) |
| 5 | GND |
| 6, 7, 8 | Eye B red, green, blue (cathodes) |

### Power

7.5 to 9 V **regulated** DC, centre-positive at the panel jack, wired to CN1 (pin 1 ground, pin 2 positive). No battery. No reverse-polarity protection on the board.

---

## How it works

```
Browser --WiFi--> ESP32-S3 (192.168.4.1) --UART 9600--> dsPIC30F4013 --> audio PWM + LEDs
```

- The dsPIC runs two sine oscillators at 31.25 kHz. The left ear gets the carrier (default 200 Hz), the right ear gets carrier + beat (default 4 Hz); the brain hears the difference as a slow pulse.
- The LEDs flash at the beat rate, locked to the audio.
- A session sequencer varies the beat: every 8 minutes it doubles for 40 s, every 13 minutes it halves for 90 s, with 3 s ramps.

More in [`docs/architecture.md`](docs/architecture.md). The command set is in [`docs/uart_protocol.md`](docs/uart_protocol.md).

---

## Firmware

| Part | Source | Build |
|---|---|---|
| dsPIC30F4013 | [`firmware/dspic/`](firmware/dspic/) | MPLAB X + XC16 (level 1), PICkit 4 on H1 |
| ESP32-S3 | [`firmware/esp32/`](firmware/esp32/) | PlatformIO: `pio run -e goggle -t upload` |

Flashing instructions: [`docs/programming.md`](docs/programming.md).

### Using it

1. Join WiFi `BinauralGoggle` (password is `AP_PASS` in `firmware/esp32/src/main.cpp`).
2. Open `http://192.168.4.1`.
3. Firmware updates after the first flash go over WiFi at `http://192.168.4.1/update`.

---

## Repository layout

```
docs/                     review, bring-up checklist, architecture, protocol, programming
firmware/dspic/src/       dspic_firmware.c, dds_tables.h
firmware/dspic/test/      PC-side logic test
firmware/dspic/tools/     table generator
firmware/esp32/           PlatformIO project (src/main.cpp)
hardware/schematic/       schematic PDF, EasyEDA sources, Gerbers
hardware/pcb/             BOM, pick-and-place, renders
hardware/enclosure/       OpenSCAD source and STLs
```

---

## Enclosure

Printed by JLC3DP in black ASA. Current source is `hardware/enclosure/scad/enclosure_v10_3.scad`: keystone RJ45 coupler for the goggle cable, 3.5 mm panel jack for headphones. Print the test coupon (`test_panel_v10_3.stl`) before the body. Older versions (v7, v8) are kept for reference; `hardware/enclosure/README.md` still describes v7.

---

*Brad Beidler · 2026 · CC BY-NC 4.0*
