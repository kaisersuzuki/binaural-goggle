# System Architecture

## Block diagram

```
┌─────────────────────────────────────────────────────────────────────┐
│                      BINAURAL GOGGLE CONTROLLER                     │
└─────────────────────────────────────────────────────────────────────┘

        9V battery
            │
        ┌───┴────┐
        │ MAX603 │── 3.3V ──┬─→ dsPIC VDD/AVDD
        │  LDO   │          ├─→ ESP32 3V3
        └────────┘          └─→ AD5220 / HC14 logic
            │
        ┌───┴────┐
        │ MAX603 │── 5.0V ──┬─→ NTE941M op-amp VCC
        │ (SET)  │          └─→ RGB LED supply
        └────────┘

  ┌─────────────────┐   WiFi    ┌──────────────────┐
  │  Phone/Laptop   │◄─────────►│  ESP32-WROOM-32E │
  │  (web browser)  │           │  (web server)    │
  └─────────────────┘           └────────┬─────────┘
                                         │ UART2 @ 115200
                                         │
                                ┌────────▼─────────┐
                                │  dsPIC30F4013    │
                                │  (PDIP-40)       │
                                │                  │
                                │  • DDS @ 8kHz    │
                                │  • Beat seq      │
                                │  • Noise gen     │
                                │  • SoftPWM LEDs  │
                                └──┬──────┬───┬────┘
                                   │      │   │
            ┌──── PWM ─────────────┘      │   └─── 6× GPIO ───┐
            │                             │                    │
            ▼                             ▼                    ▼
   ┌─────────────────┐         ┌─────────────────┐    ┌──────────────┐
   │  RC LPF (×2)    │         │  AD5220 SPI pot │    │  HC14 + 2N3904│
   │  Fc≈1.6kHz      │         │  (master vol)   │    │  RGB drivers  │
   └────────┬────────┘         └────────┬────────┘    └──────┬───────┘
            │                           │                    │
            ▼                           │                    ▼
   ┌─────────────────┐                  │           ┌──────────────┐
   │  NTE941M unity  │◄─────────────────┘           │  RGB LEDs    │
   │  buffers (×2)   │                              │  (in goggles)│
   └────────┬────────┘                              └──────────────┘
            │
            ▼
   ┌─────────────────┐
   │  3.5mm jack     │
   │  → headphones   │
   └─────────────────┘
```

## Why two MCUs

The dsPIC handles all hard-real-time work — running a DDS oscillator at 8 kHz with sample-accurate phase, generating noise, and managing LED PWM all from interrupts. Adding WiFi to that chip would either require a heavier processor or constant priority juggling.

The ESP32 handles only soft-real-time work: receive a web request, parse it, send an ASCII line over UART. It can take its time without affecting audio quality.

This separation also means the audio engine keeps running cleanly even if WiFi drops, the user's phone disconnects, or the ESP32 is OTA-updating itself.

## Programming workflow

| Component | First flash | Updates |
|---|---|---|
| dsPIC30F4013 | PICkit 4 + MPLAB X via J1 ICSP header | PICkit 4 (rare — firmware is stable) |
| ESP32-WROOM-32E | USB-UART adapter via 6-pin programming header | OTA over WiFi via web UI |

## Power

- **9V battery** (or DC jack) → switch → MAX603 ×2 → 3.3V and 5V rails
- **MAX8212** monitors battery voltage and signals dsPIC AN0 when low
- All bypass caps per datasheet
