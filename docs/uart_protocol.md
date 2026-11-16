# UART protocol: ESP32-S3 to dsPIC30F4013 (firmware v3.0)

## Physical layer

| Parameter | Value |
|---|---|
| Baud rate | 9600, 8N1 |
| Logic level | 3.3 V |
| Net `RX0` (H3 pin 6) | ESP32-S3 GPIO44 **transmits**, dsPIC pin 26 (U1RX / RF2) receives |
| Net `TX0` (H3 pin 5) | dsPIC pin 25 (U1TX / RF3) **transmits**, ESP32-S3 GPIO43 receives |

The net names are misleading. The PCB joins the ESP32 module's TXD0 pad to the dsPIC's transmit pin and RXD0 to its receive pin, so the ESP32 firmware swaps its two pins in software (UART1 mapped onto GPIO43/44). See `hardware_review_rev1.1.md`, item 8.

Rules that follow from sharing those nets with the ESP32 boot ROM and the programming header:

- The dsPIC only transmits in reply to a valid command. It sends no start-up banner.
- The dsPIC's transmit pin is high-impedance except while a reply is going out. The ESP32 enables a pull-up on GPIO43.
- A line that is not a known command gets **no reply**. The ESP32 reports that as `NO REPLY`.
- The ESP32 sends an empty line before each command to clear any half-received noise.

## Commands (ESP32 to dsPIC)

All ASCII, upper case, terminated by `\n`. Maximum 38 characters.

| Command | Range | Reply | Effect |
|---|---|---|---|
| `BEAT:<hz>` | 0.5 to 40.0 | `OK:BEAT` | Beat rate. Ramps over 3 s. Ends any sequencer event in progress. |
| `CARRIER:<hz>` | 40 to 1000 | `OK:CARRIER` | Left-ear tone. Right ear is carrier + beat. |
| `VOL:<n>` | 0 to 100 | `OK:VOL` | Volume in percent, 0.48 dB per step, ramped. 0 is silence. |
| `AMP:ON` / `AMP:OFF` | | `OK:AMP` | Headphone amplifier enable. ON restarts from silence and ramps up. |
| `RGB_A:<r>,<g>,<b>` | 0 to 255 each | `OK:RGB_A` | Eye A colour (fades). |
| `RGB_B:<r>,<g>,<b>` | 0 to 255 each | `OK:RGB_B` | Eye B colour. |
| `RGB_AB:<r>,<g>,<b>` | 0 to 255 each | `OK:RGB_AB` | Both eyes. |
| `MODE:ALT` / `SYNC` / `SOLID` | | `OK:MODE` | ALT: eyes flash on opposite halves of the beat. SYNC: together. SOLID: steady. |
| `DUTY:<n>` | 5 to 100 | `OK:DUTY` | Percent of each flash window the light is on. |
| `SEQ:ON` / `SEQ:OFF` | | `OK:SEQ` | Session sequencer (below). Restarts its timers. |
| `STATUS` | | status line | Current state. |
| `RESET` | | `OK:RESET` | Beat 4 Hz, carrier 200 Hz, sequencer on, duty 100, mode ALT. Volume, colours and amplifier are left alone. |

Error replies, only for a recognised command with a bad argument: `ERR:RANGE`, `ERR:PARSE`.

Status line:

```
BEAT:4.0 CAR:200.0 VOL:30 MODE:ALT SEQ:ON DUTY:100 AMP:ON FW:3.0
```

`BEAT` is the rate playing now, which differs from the set rate during a ramp or a sequencer event.

## Session sequencer

With `SEQ:ON`, relative to the rate set by `BEAT` (default 4 Hz):

- every 8 minutes the beat rises to **2x** for 40 s (default 8 Hz)
- every 13 minutes it falls to **half** for 90 s (default 2 Hz)
- every change ramps over 3 s

Limits of 0.5 and 40 Hz still apply. Use `SEQ:OFF` to hold one rate, which is what you want for light-only (photic) sessions.

## Timing

- Replies start within a few milliseconds; `STATUS` takes about 75 ms to send.
- The ESP32 waits 400 ms for a reply.

## Beat ranges

| Range | Band | Usual association |
|---|---|---|
| 0.5 to 4 Hz | Delta | Deep sleep |
| 4 to 8 Hz | Theta | Meditation, drowsiness |
| 8 to 14 Hz | Alpha | Relaxed focus |
| 14 to 30 Hz | Beta | Active thinking |

Flashing light between about 15 and 25 Hz carries the highest risk for people with photosensitive epilepsy.
