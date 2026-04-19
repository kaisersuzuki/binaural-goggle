# UART Protocol — ESP32 ↔ dsPIC30F4013

## Physical Layer

| Parameter | Value |
|-----------|-------|
| Baud rate | 9600 |
| Format | 8N1 |
| ESP32 pin (TX) | GPIO17 (Serial2 TX) |
| ESP32 pin (RX) | GPIO16 (Serial2 RX) |
| dsPIC pin (RX) | RF4 / U2RX |
| dsPIC pin (TX) | RF5 / U2TX |
| Logic level | 3.3V (both devices) |

## Message Format

All messages are ASCII, newline (`\n`) terminated.
ESP32 sends commands; dsPIC responds.

## Commands (ESP32 → dsPIC)

| Command | Example | Description |
|---------|---------|-------------|
| `BEAT:<hz>` | `BEAT:4.0` | Set beat frequency (0.5–40.0 Hz) |
| `CARRIER:<hz>` | `CARRIER:200.0` | Set carrier frequency (50.0–500.0 Hz) |
| `VOL:<0-63>` | `VOL:32` | Set volume (AD5220 step position) |
| `RGB_A:<r>,<g>,<b>` | `RGB_A:255,0,0` | Set eye A color (0–255 each channel) |
| `RGB_B:<r>,<g>,<b>` | `RGB_B:0,0,255` | Set eye B color |
| `RGB_AB:<r>,<g>,<b>` | `RGB_AB:128,0,128` | Set both eyes same color |
| `STATUS` | `STATUS` | Request current state |
| `RESET` | `RESET` | Reset sequencer to 4Hz baseline |

## Responses (dsPIC → ESP32)

| Response | Meaning |
|----------|---------|
| `OK:BEAT` | Beat frequency accepted |
| `OK:CARRIER` | Carrier frequency accepted |
| `OK:VOL` | Volume set |
| `OK:RGB_A` | Eye A color set |
| `OK:RGB_B` | Eye B color set |
| `OK:RGB_AB` | Both eyes set |
| `OK:RESET` | Sequencer reset |
| `BEAT:x.x CARRIER:x.x VOL:xx` | Status response |
| `ERR:UNKNOWN` | Unrecognized command |
| `READY:BINAURAL_GOGGLE_V2` | Boot message (sent once on power-up) |

## Timing Notes

- dsPIC responds within ~1ms for most commands
- Volume changes (`VOL:`) take longer if stepping many positions (64 steps max = ~5ms)
- ESP32 waits up to 200ms for a response before returning "timeout" to the web UI
- STATUS response is always sent within 5ms

## Beat Frequency Ranges

| Range | Brainwave | Effect |
|-------|-----------|--------|
| 0.5–4 Hz | Delta | Deep sleep |
| 4–8 Hz | Theta | Meditation, drowsiness |
| 8–14 Hz | Alpha | Relaxed focus |
| 14–30 Hz | Beta | Active thinking |

Default session pattern:
- **4Hz theta** baseline
- **8Hz spike** every ~8 minutes (40 second duration)
- **2Hz dip** every ~13 minutes (90 second duration)
- All transitions ramp over 3 seconds
