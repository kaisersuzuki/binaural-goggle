# dsPIC30F4013 Firmware

The audio + LED engine. Generates binaural beats via DDS, drives RGB LEDs via soft-PWM, takes commands from the ESP32 over UART1.

## Toolchain

- **MPLAB X IDE** (free from microchip.com)
- **XC16 compiler** v2.x or later (free)
- **PICkit 4** programmer (~$50, DigiKey)

## Building

1. Open MPLAB X IDE → **File → New Project → Microchip Embedded → Standalone Project**
2. Family: **16-bit DSCs (dsPIC30)**, Device: **dsPIC30F4013**
3. Tool: **PICkit 4**, Compiler: **XC16**
4. Project name: `binaural_goggle_dspic`
5. Right-click *Source Files* → Add Existing Item → select `src/dspic_firmware.c`
6. **Build** (hammer icon)

## Flashing

1. Connect PICkit 4 to your Mac via USB
2. Connect PICkit 4 cable to PCB **J1** (5-pin ICSP header)
3. Pin orientation on J1: MCLR / VDD / VSS / PGD / PGC (pin 1 marked)
4. In MPLAB X click the **Make and Program Device** icon (downward arrow)
5. Watch the output pane — should see "Programming complete"

## Configuration

Edit constants near the top of `dspic_firmware.c`:

```c
#define SAMPLE_RATE         8000U     // DDS interrupt rate
#define DEFAULT_CARRIER_HZ  128       // Initial carrier
#define DEFAULT_BEAT_HZ     4.0f      // Initial beat
```

The runtime command interface (over UART1 from the ESP32) lets you change these without reflashing — see [`docs/uart_protocol.md`](../../docs/uart_protocol.md).

## Pin map

See header comment block in `src/dspic_firmware.c` for the full pin assignments. Summary:

| Pin | Function |
|---|---|
| RD0 (OC1) | Left audio PWM |
| RD1 (OC2) | Right audio PWM |
| RF6/RF7 + RB2/RB3 | SPI to AD5220 volume pot |
| RF2/RF3 | UART1 to ESP32 |
| RG2/3/4 | Left eye R/G/B LEDs |
| RG6/7/8 | Right eye R/G/B LEDs |
| AN0 | Battery voltage divider |
