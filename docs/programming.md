# Programming guide (PCB rev 1.1, firmware v3.0)

Do the board rework in [`bringup_checklist.md`](bringup_checklist.md) first. Without it the ESP32 cannot start and the dsPIC's reset pin floats.

## Tools

- **PICkit 4**, **MPLAB X IDE** and the **XC16** compiler for the dsPIC
- **PlatformIO** (VS Code extension or `pip install platformio`) for the ESP32-S3
- A 3.3 V **USB-UART adapter** (CP2102, CH340, FTDI) for the first ESP32 flash

## ESP32-S3 (U3, ESP32-S3-WROOM-1-N8R8)

The board has no USB connector and no BOOT or RESET buttons. The first flash goes through header H3:

| H3 pin | Label | Connect to |
|---|---|---|
| 1 | GND | adapter GND |
| 2 | 3V3 | nothing (power the board from its own supply) |
| 3 | EN | jumper to GND briefly to reset |
| 4 | IO0 | jumper to GND while powering on, for download mode |
| 5 | TX0 | adapter RX |
| 6 | RX0 | adapter TX |

1. Jumper IO0 to GND, power on, remove the jumper.
2. `cd firmware/esp32 && pio run -e goggle -t upload`
3. **Remove the adapter's TX wire from H3 pin 6**, then power-cycle. In normal running the ESP32 drives that net, and a connected adapter would fight it.

The dsPIC can stay in its socket (with v3.0 dsPIC firmware, or blank).

There is no serial monitor. H3's two data pins are the only UART pads wired on the module and they carry the dsPIC link. Debug output is at `http://192.168.4.1/log`.

### Later updates, over WiFi

Join the `BinauralGoggle` network, then either:

- open `http://192.168.4.1/update` (user `admin`, password = `OTA_PASS` in `main.cpp`) and upload `.pio/build/goggle/firmware.bin`, or
- `pio run -e goggle_ota -t upload`

## dsPIC30F4013 (U1)

### Build

1. MPLAB X: File, New Project, Standalone Project. Device **dsPIC30F4013**, tool **PICkit 4**, compiler **XC16**.
2. Add `firmware/dspic/src/dspic_firmware.c` to Source Files. `dds_tables.h` must stay in the same folder as the `.c` file.
3. Project Properties, xc16-gcc, Optimizations: level **1**. The 31 kHz sample interrupt has not been timed at level 0, so do not leave it at the default.
4. Build.

### Flash

H1 is a 2x3 header, so the PICkit does not plug straight in. Use five jumper wires:

| PICkit 4 pin | Signal | H1 pin |
|---|---|---|
| 1 | MCLR / VPP | 1 |
| 2 | VDD (sense) | 3 |
| 3 | GND | 5 |
| 4 | PGD | 2 |
| 5 | PGC | 4 |

Power the board from its own supply; do not power the target from the PICkit. Then "Make and Program Device".

The LEDs flicker during programming because PGC and PGD are also two LED outputs. That is harmless.

### If programming fails at 3.3 V

The dsPIC30F's bulk-erase command is specified for VDD of 4.5 V or more, and the board runs the chip at 3.3 V. Programming usually still works with code protection off (it is off in this firmware; do not turn it on). If MPLAB reports an erase or verify failure, take the chip out and program it on a breadboard at 5 V: pins 11, 21, 32, 40 to VDD, pins 12, 20, 31, 39 to GND, 10 kΩ from pin 1 to VDD, PICkit to pins 1 (MCLR), 9 (PGD), 8 (PGC), and let the PICkit power the target at 5 V.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| No `BinauralGoggle` network | IO0 still held low by U5 (measure H3 pin 4: must be above 3 V), or the upload did not finish |
| esptool: "Wrong chip" or "This chip is ESP32-S3" | Building an old `esp32dev` environment. Use `-e goggle` |
| esptool: "Failed to connect" | Not in download mode: IO0 must be low at the moment power comes on. TX/RX swapped at the adapter |
| Web page says "dsPIC NOT RESPONDING" | dsPIC not programmed, MCLR pull-up missing, or v2.2 firmware still on the chip |
| PICkit: "Target device was not found" | Wiring to H1, board not powered, or MCLR pull-up shorted |
| Lights never come on | LED resistors are 1.2 MΩ (review item 3), LEDs are common-cathode (item 4), or 74HC14 threshold (item 16) |
| ESP32 resets when WiFi starts | Supply sagging. Check 3V3 under load; fit the EN capacitor |
