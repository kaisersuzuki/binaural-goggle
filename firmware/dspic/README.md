# dsPIC30F4013 firmware (v3.0)

The audio and light engine. Generates the two tones, drives the six LED channels, sets the volume pots, and takes text commands from the ESP32-S3 over UART1.

v3.0 was rewritten against the netlist of the rev 1.1 board. The reasons are in [`docs/hardware_review_rev1.1.md`](../../docs/hardware_review_rev1.1.md).

## Files

| File | Purpose |
|---|---|
| `src/dspic_firmware.c` | The firmware. The header comment has the pin map and protocol summary |
| `src/dds_tables.h` | Generated sine, volume and pot tables. Must sit next to the `.c` file |
| `tools/gen_tables.py` | Regenerates `dds_tables.h` if the volume law or component values change |
| `test/host_test.c`, `test/xc.h` | PC-side test of the firmware logic (no chip needed) |

## Build and flash

MPLAB X + XC16, optimisation level 1, PICkit 4 on header H1. Steps and the H1 wiring are in [`docs/programming.md`](../../docs/programming.md).

Not yet compiled with XC16 (it was not available when v3.0 was written). Register and configuration names were checked against Microchip's dsPIC30F4013 device header. If the build reports an unknown name, that is the place to look.

## Settings worth knowing

Near the top of `dspic_firmware.c`:

```c
#define DEFAULT_BEAT    4.0f     /* Hz */
#define DEFAULT_CARRIER 200.0f   /* Hz */
#define DEFAULT_VOL     30U      /* percent */
#define AMP_AT_BOOT     1        /* 0 if the TPA6132A2 is bypassed */
```

Everything else is set at run time; see [`docs/uart_protocol.md`](../../docs/uart_protocol.md).

## Pin map (rev 1.1)

| Pin | Function |
|---|---|
| 34 RD0 (OC1) / 33 RD1 (OC2) | Left / right audio PWM, 31.25 kHz |
| 4 RB2, 5 RB3, 24 RF6 | AD5220 /CS, U/D, CLK (both pots) |
| 37 RB10 | TPA6132A2 enable |
| 26 RF2 (U1RX), 25 RF3 (U1TX) | UART1 to the ESP32-S3 |
| 6 RB4, 7 RB5, 8 RB6 | Eye A red, green, blue |
| 9 RB7, 10 RB8, 38 RB9 | Eye B red, green, blue |
| 13, 14 | 10 MHz crystal (x4 PLL, 10 MIPS) |
| 1 | MCLR. Needs the 10 kΩ pull-up added by hand |

## Running the PC-side test

```sh
cd firmware/dspic/test
clang -std=c99 -Wall -Wextra -I. -o /tmp/host_test host_test.c -lm && /tmp/host_test
```

It checks tone frequencies, PWM range, LED timing in each mode, the command parser (including that noise gets no reply), the volume law and the sequencer. It cannot check interrupt timing or anything electrical.
