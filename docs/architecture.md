# System architecture (PCB rev 1.1, firmware v3.0)

```
  Phone / laptop browser
        |  WiFi (AP "BinauralGoggle", 192.168.4.1)
        v
  ESP32-S3-WROOM-1 (U3) ......... web page, /cmd, /log, /update
        |  UART, 9600 8N1, ASCII lines (pins swapped in software)
        v
  dsPIC30F4013 (U1), 10 MIPS at 3.3 V
        |
        +-- OC1/OC2 PWM, 31.25 kHz --> RC filter --> AD5220 pot x2 --> TPA6132A2 --> H4 --> headphones
        |        (two sine oscillators: carrier, carrier + beat)
        |
        +-- RB2/RB3/RF6 ------------> AD5220 step control (both pots together)
        +-- RB10 -------------------> TPA6132A2 enable
        |
        +-- RB4..RB9 --> 74HC14 x2 (buffers) --> 2N3904 x6 --> 100 R --> H5 --> goggle LEDs
                 (low-side switches; LEDs are common anode on +5 V)
```

## Why two processors

The dsPIC does the time-critical work: one interrupt at 31.25 kHz computes both sine oscillators and, every fourth sample, the LED outputs. The ESP32-S3 only translates web requests into text commands. The tone and lights keep running if WiFi drops or the ESP32 is being updated.

## Inside the dsPIC firmware

| Rate | Where | What |
|---|---|---|
| 31.25 kHz | Timer2 interrupt | Two 32-bit phase accumulators, 1024-point sine table, amplitude scaling, PWM duty with error feedback |
| 7.8 kHz | same interrupt, every 4th sample | LED flash gate from the phase difference of the two oscillators; 32-step brightness PWM (244 Hz) |
| 30.5 Hz | main loop | Beat ramp, session sequencer, colour fade, volume ramp. All floating-point maths lives here |
| on demand | main loop | Command parser and replies |

Volume is split in two: the AD5220 pots (128 steps) set the coarse level after the filter, and the firmware trims the sine amplitude between pot steps. That gives 0.48 dB steps while keeping the PWM near full scale, where its noise is lowest relative to the tone.

## Power

- 7.5 to 9 V regulated DC on CN1 (pin 1 ground, pin 2 positive). No battery, no reverse-polarity protection.
- U6 LD1117AG-3.3: 3.3 V for the dsPIC and the ESP32-S3.
- U4 AMS1117-5.0: 5 V for the LEDs, 74HC14s, AD5220s and the amplifier.
- U5 MAX8212 was meant as a supply monitor but its output holds the ESP32's boot pin low; it has to be disconnected. See `hardware_review_rev1.1.md`.

## Programming

| Part | First flash | Updates |
|---|---|---|
| dsPIC30F4013 | PICkit 4 on H1 | PICkit 4 |
| ESP32-S3 | USB-UART adapter on H3 | WiFi: `/update` page or `pio run -e goggle_ota -t upload` |

Details in [`programming.md`](programming.md); protocol in [`uart_protocol.md`](uart_protocol.md).
