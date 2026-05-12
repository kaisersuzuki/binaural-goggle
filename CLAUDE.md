# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build & Flash

**ESP32** (VS Code + PlatformIO):
```bash
cd firmware/esp32
pio run                        # build
pio run --target upload        # initial wired flash via USB-UART on J2 header
pio run --target monitor       # serial monitor at 115200 baud (USB debug)
# Subsequent updates: OTA via http://192.168.4.1/update
```

**dsPIC30F4013**: MPLAB X IDE + XC16 compiler only. Open `firmware/dspic/src/dspic_firmware.c`, create a project targeting `dsPIC30F4013` with XC16, flash via PICkit 4 connected to J1 (ICSP header). No CLI build.

## Architecture

Two-MCU split: dsPIC owns all hard-real-time work; ESP32 owns all network work.

```
Browser → ESP32 (WiFi AP 192.168.4.1) → UART2 @ 9600 8N1 → dsPIC30F4013
```

**dsPIC30F4013** (`firmware/dspic/src/dspic_firmware.c`):
- 10MHz crystal × 4 PLL = 20MHz Fcy
- Timer1 ISR at 8kHz drives DDS sine generation (left/right channels at slightly offset frequencies to produce the binaural beat)
- OC1/OC2 output PWM at ~256kHz → RC low-pass filter (Fc ≈ 1.6kHz) → NTE941M unity buffer → 3.5mm jack
- 6× GPIO → HC14 Schmitt inverters → 2N3904 NPN transistors → RGB LEDs (3 per eye, 2 eyes)
- SPI to AD5220BRZ50 digital potentiometer for master volume (64 steps)
- UART2 (RF4/RF5) receives ASCII commands from ESP32, responds `OK:<cmd>` or `ERR:UNKNOWN`
- Beat sequencer runs autonomously: 4Hz baseline, 8Hz spike every ~8 min (40s), 2Hz dip every ~13 min (90s), 3s ramps

**ESP32-WROOM-32E** (`firmware/esp32/src/main.cpp`):
- Arduino framework, ESPAsyncWebServer + ArduinoJson
- AP mode: SSID `BinauralGoggle`, password `meditate123`, IP `192.168.4.1`
- Web UI parses user input → sends UART2 ASCII command → waits up to 200ms for dsPIC response → returns result to browser
- Serial2 (GPIO16 RX / GPIO17 TX) ↔ dsPIC; Serial (USB) for debug only
- OTA at `/update` (hostname `binaural-goggle`, password `goggle-ota`)

## UART Command Reference

Commands are ASCII newline-terminated, ESP32 → dsPIC:

| Command | Range |
|---|---|
| `BEAT:<hz>` | 0.5–40.0 Hz |
| `CARRIER:<hz>` | 50.0–500.0 Hz |
| `VOL:<n>` | 0–63 |
| `RGB_A:<r>,<g>,<b>` | 0–255 each |
| `RGB_B:<r>,<g>,<b>` | 0–255 each |
| `RGB_AB:<r>,<g>,<b>` | 0–255 each |
| `STATUS` | returns `BEAT:x.x CARRIER:x.x VOL:xx` |
| `RESET` | resets sequencer to 4Hz baseline |

dsPIC boots with `READY:BINAURAL_GOGGLE_V2`.

## Key Hardware Notes

- RB6/RB7 are shared with PGC/PGD — LEDs flash briefly during dsPIC programming, expected
- 9V input → LD1117AG (3.3V for logic) + AMS1117-5.0 (5V for LEDs/amp); no battery
- PCB files and Gerbers in `hardware/schematic/`; use v8_FINAL STLs for enclosure
- `enclosure/scad/enclosure_v7.scad` is the parametric source (v8 FINAL STLs are the print-ready output)
