# UART Command Protocol

ASCII commands sent from ESP32 to dsPIC over UART1 at **115200 8N1**, newline-terminated (`\r\n`).

## Format

```
KEY:VALUE\n
```

Single command per line. dsPIC responds with `OK\r\n` on success or `ERR:<msg>\r\n` on failure.

## Commands

### Audio

| Command | Range | Description |
|---|---|---|
| `CARRIER:<hz>` | 32, 64, 128, 256, 512 | Carrier frequency for binaural beat |
| `BEAT:<hz>` | 0.5–12.0 | Beat frequency (L=carrier-beat/2, R=carrier+beat/2) |
| `VOL:<n>` | 0–63 | AD5220 wiper position (master volume) |

### Noise

| Command | Range | Description |
|---|---|---|
| `NOISE:<type>` | OFF, PINK, BROWN, BLUE, WHITE | Background noise color |
| `MIX:<n>` | 0–100 | Tone/noise ratio (100 = tone only, 0 = noise only) |

### Modulation

| Command | Range | Description |
|---|---|---|
| `ISOCHRONIC:<n>` | 0–100 | AM modulation depth (0 = none, 100 = full pulse) |
| `PATTERN:<state>` | ON, OFF | Auto spike-dip beat sequencing |

### LEDs

| Command | Range | Description |
|---|---|---|
| `LED_MODE:<mode>` | OFF, RGB, FOLLOW | LED behavior |
| `LED_COLOR:<r>,<g>,<b>` | 0–255 each | Manual RGB override |
| `LED_BRIGHT:<n>` | 0–255 | Master LED brightness |

### Presets & state

| Command | Description |
|---|---|
| `PRESET:<name>` | Load named preset (THETA, ALPHA, SMR, MEDITATE) |
| `STATUS?` | Returns `STATUS:{...}` JSON of current parameters |

## Examples

```
CARRIER:128
BEAT:4.0
NOISE:PINK
MIX:70
ISOCHRONIC:50
LED_COLOR:0,0,255
LED_BRIGHT:128
STATUS?
```

Response to `STATUS?`:

```
STATUS:{"carrier":128,"beat":4.0,"vol":32,"noise":1,"mix":70,
        "iso":50,"led":1,"rgb":[0,0,255],"bright":128}
```

## Notes

- All commands are case-sensitive
- Whitespace around values is tolerated
- Out-of-range values are clamped
- Multiple commands can be sent rapidly — dsPIC parser handles backpressure via UART hardware FIFO
