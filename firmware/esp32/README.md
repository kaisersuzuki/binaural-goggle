# ESP32 Firmware

WiFi controller and web UI host. Receives commands from a browser over WiFi, translates them to UART ASCII commands for the dsPIC.

> **Status:** Smoke-test foundation only. Verified working end-to-end on ESP32 DevKitC (April 2026). Production version will add full command set, OTA updates, SPIFFS-served UI, and STA mode fallback.

## Toolchain

- **VS Code** with the **PlatformIO IDE** extension
- **CH340 USB-UART driver** (Mac): `brew install --cask wch-ch34x-usb-serial-driver`

## Building & flashing (during development)

1. Open this folder (`firmware/esp32/`) in VS Code with PlatformIO installed
2. Plug in the ESP32 DevKitC via USB
3. Click the **→** (Upload) icon in the bottom blue status bar
4. Click the **🔌** (Serial Monitor) icon to watch logs

## Flashing the final PCB (one-time)

The production PCB exposes a 6-pin programming header with `GND / 3V3 / EN / IO0 / TX0 / RX0`. To flash the initial firmware:

1. Connect a USB-UART adapter (FTDI or CP2102) to the header
2. Hold BOOT, tap RESET, release BOOT to enter download mode
3. `pio run --target upload` (or use the upload arrow in VS Code)
4. After this first flash, all subsequent updates happen via WiFi (OTA) — see below

## Using the web UI

1. After flashing, the ESP32 creates a WiFi network: **BinauralGoggle** (password: `meditate123`)
2. Connect from any phone/laptop
3. Browse to `http://192.168.4.1`
4. Adjust sliders, hit APPLY
5. Watch the serial monitor — you'll see the UART command that would be sent to the dsPIC

## Architecture

```
[Browser] ─HTTP─→ [ESP32 web server] ─UART2─→ [dsPIC30F4013]
              ←─events─                ←──────
```

- **Web server:** ESPAsyncWebServer on port 80
- **UI:** Single-page HTML/CSS/JS (currently inline, will move to SPIFFS)
- **UART bridge:** Translates JSON/form params to ASCII commands per [UART protocol](../../docs/uart_protocol.md)
- **OTA:** ArduinoOTA library, firmware updates via WiFi after initial flash

## Pin map (final PCB)

| ESP32 GPIO | Function |
|---|---|
| 1 (TX0)  | Programming header — initial flash only |
| 3 (RX0)  | Programming header — initial flash only |
| 16 (RX2) | UART2 from dsPIC TX (RF3) |
| 17 (TX2) | UART2 to dsPIC RX (RF2) |
| EN       | Reset (boot button) |
| 0        | Boot mode select (boot button) |
