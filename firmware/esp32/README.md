# ESP32-S3 firmware (v3.0)

WiFi access point and web page. Turns browser actions into text commands for the dsPIC.

The module on the rev 1.1 board is an **ESP32-S3-WROOM-1-N8R8**. Earlier versions of this project were written for an ESP32-WROOM-32E and will not flash or run.

## Build and flash

```sh
cd firmware/esp32
pio run -e goggle                 # build
pio run -e goggle -t upload       # first flash, USB-UART adapter on H3
pio run -e goggle_ota -t upload   # later, over WiFi
```

Wiring for the first flash, and the board rework that must come before it, are in [`docs/programming.md`](../../docs/programming.md) and [`docs/bringup_checklist.md`](../../docs/bringup_checklist.md).

Build status: compiles cleanly with PlatformIO `espressif32`, board `esp32-s3-devkitc-1` (778 kB, 23 % of the app partition). Not yet run on the board.

## Using it

1. Join WiFi **BinauralGoggle** (password `AP_PASS` in `src/main.cpp`).
2. Open `http://192.168.4.1`.
3. The top line shows whether the dsPIC is answering. Controls send as you release them.

| URL | Purpose |
|---|---|
| `/` | Control page |
| `/cmd?c=<COMMAND>` | Send one command, returns the dsPIC's reply or `NO REPLY` |
| `/log` | Last 32 commands and replies |
| `/update` | Firmware upload (user `admin`, password `OTA_PASS`) |

## Pins

| Module pad | GPIO | Use |
|---|---|---|
| TXD0 (H3 pin 5, net `TX0`) | 43 | **Receive** from the dsPIC |
| RXD0 (H3 pin 6, net `RX0`) | 44 | **Transmit** to the dsPIC |
| IO0 (H3 pin 4) | 0 | Boot mode strap only |
| EN (H3 pin 3) | | Reset |

The swap is deliberate: the PCB wires transmit to transmit. The firmware uses UART1 on those pins so that nothing the Arduino core prints on UART0 reaches the dsPIC. No other GPIO is connected on this board.

There is no serial monitor and no VIN read-out; see the review document for why.

## Changing the WiFi or update passwords

Edit `AP_PASS` and `OTA_PASS` in `src/main.cpp`, and `--auth` in `platformio.ini` to match. The defaults are published in this repository, so anyone nearby who has read it can join the goggle's network.
