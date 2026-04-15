# Programming Guide

How to flash firmware on both MCUs.

## Required tools

- **PICkit 4** programmer for the dsPIC (~$50 from DigiKey/Microchip)
- **MPLAB X IDE** + **XC16 compiler** for dsPIC firmware (free)
- **VS Code** + **PlatformIO** for ESP32 firmware (free)
- **CH340/CP2102 USB-UART adapter** for initial ESP32 flash (~$5)
- **CH340 driver** (Mac): `brew install --cask wch-ch34x-usb-serial-driver`

## Order of operations

1. Solder the dsPIC30F4013 onto the assembled PCB (it's a hand-soldered through-hole part)
2. Flash the dsPIC via the J1 ICSP header
3. Flash the ESP32 via the 6-pin programming header
4. From here on, ESP32 firmware updates happen over WiFi (OTA)

## Flashing the dsPIC

1. Open `firmware/dspic/src/dspic_firmware.c` in MPLAB X (create a new project targeting **dsPIC30F4013** with **XC16** compiler)
2. Connect PICkit 4 → Mac via USB
3. Connect PICkit 4 → PCB **J1** (5-pin ICSP header). Pin 1 is marked on the board
4. Hit **Make and Program Device** (downward arrow icon)
5. Watch the output pane for "Programming complete"

If you get a "device not detected" error, check the J1 connector orientation and that the dsPIC has both VDD (3.3V) and VSS (GND) connected.

## Flashing the ESP32 (initial)

1. Connect USB-UART adapter to the 6-pin programming header on the PCB:
   - Adapter GND  → Header GND
   - Adapter 3V3  → Header 3V3 (or leave disconnected if board is powered via main supply)
   - Adapter TX   → Header RX0
   - Adapter RX   → Header TX0
   - Adapter DTR  → Header EN  (optional, for auto-reset)
   - Adapter RTS  → Header IO0 (optional, for auto-boot)
2. Hold **BOOT** button on PCB, tap **RESET**, release **BOOT**
3. In `firmware/esp32/`: `pio run --target upload`
4. After upload, tap **RESET** to start running

## Flashing the ESP32 (OTA, after first flash)

1. Connect to the WiFi network the ESP32 is on (either AP mode or your home network)
2. In your browser, go to the device's address (default: `http://192.168.4.1/update`)
3. Upload the new `firmware.bin` (built by `pio run`)
4. Device reboots automatically when complete

OTA is the normal update path — you should rarely need the wired programming header after the initial flash.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| PICkit 4 not detected | USB cable issue, or PICkit 4 needs a firmware update via MPLAB X |
| `device ID mismatch` | Wrong device selected in MPLAB X project — must be exactly dsPIC30F4013 |
| ESP32 upload "Connecting..." hangs | BOOT/RESET sequence wrong, or auto-reset wiring missing |
| Garbled serial output | Baud rate mismatch — check both ends are 115200 |
| WiFi network doesn't appear | Check `WiFi.softAP()` succeeded in serial logs |
| OTA upload fails | Check device IP, ensure same network, firewall blocking port 3232 |
