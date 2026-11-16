# Hardware and firmware review: PCB rev 1.1 as manufactured

Reviewed 2026-11-15 against the files JLCPCB built from:
`hardware/schematic/PCB_PCB_AUDIO-CONTROLLER_2_2026-04-19.json` (netlist extracted from the pads),
`Schematic_AUDIO-CONTROLLER_2026-04-19.pdf`, and `hardware/pcb/bom/BOM_AUDIO-CONTROLLER_2026-04-19.csv`.

## Verdict

**The board will not work as it is, with any firmware.** Five things have to be fixed by hand on the board. Four of them are easy soldering jobs; the fifth (the headphone amplifier) is not, and has a workaround.

**The v2.2 firmware could not have worked on this board either.** The ESP32 build targeted the wrong chip, the dsPIC used a UART whose pins are not connected, and the link between the two is wired transmit-to-transmit. Both firmwares have been rewritten as v3.0 for the board as it really is.

With the four easy fixes and v3.0 firmware, WiFi, the web page, the dsPIC link and the lights should work. Sound depends on which amplifier option you take (section 5).

Nothing here has been tested on hardware. The findings come from the design files and datasheets; the firmware has been compiled (ESP32) and logic-tested on a PC (dsPIC), see "What was verified" at the end.

Step-by-step instructions are in [`bringup_checklist.md`](bringup_checklist.md).

---

## Must fix on the board

### 1. ESP32 never starts: GPIO0 is held low by U5

Net `GIOP0` joins the ESP32-S3 pin IO0, header H3 pin 4, and the output of U5 (MAX8212, pin 4). The MAX8212 output is an open-drain switch that turns **on** (pulls low) when its THRESH pin is above 1.15 V. THRESH sits on the 100k/33k divider from VIN, so the output pulls low whenever VIN is above about 4.6 V, which is always.

IO0 low at reset puts the ESP32-S3 in its ROM download mode. The application never runs: no WiFi network appears.

**Fix:** cut or lift pin 4 of U5 (the last pin on the same side as pin 1), or remove U5. IO0 then floats high on the chip's internal pull-up. Nothing else uses U5.

The v2.2 firmware assumed the divider itself went to GPIO0 and read it with the ADC. It does not, and GPIO0 is not an ADC pin on the ESP32-S3. No pin of the module can sense VIN on this board, so the VIN read-out is gone.

### 2. dsPIC reset pin floats: no pull-up on MCLR

Net `MCLR` connects only U1 pin 1 and H1 pin 1. The dsPIC30F has no internal pull-up on MCLR. A floating reset pin gives random resets or a chip that never leaves reset.

**Fix:** solder a 10 kΩ resistor between H1 pin 1 (MCLR) and H1 pin 3 (3V3). They are neighbouring pins in the same row of the 2x3 header; fit it on the underside so the header stays free for the PICkit.

### 3. LED resistors are probably 1.2 MΩ, not 100 Ω

The BOM line for R1, R2, R3, R16, R17, R18 says "100R" but gives manufacturer part `0603WAF1204T5E`, LCSC `C22766`. That part is a **1.2 MΩ** resistor (the 100 Ω part is `0603WAF1000T5E`, `C22775`). JLCPCB assembles by LCSC number. With 1.2 MΩ the LEDs get about 3 µA and stay dark.

**Check:** power off, measure across R1. It should read 100 Ω.
**Fix if it reads 1.2 MΩ:** solder a 100 Ω resistor on top of each of the six (in parallel with 1.2 MΩ that is 100 Ω), or replace them.

### 4. Goggle LEDs must be common-anode, and the README pin map is wrong

The board switches the LED **cathodes** to ground (NPN low-side switches) and supplies +5 V on H5 pin 4. That only works with common-anode RGB LEDs (or separate LEDs with their anodes joined to +5 V). The README says common-cathode; those cannot be driven by this board.

The README's GX16-8 table also does not match the board. The real header H5 is:

| H5 pin | Signal |
|---|---|
| 1 | Eye A red (cathode) |
| 2 | Eye A green (cathode) |
| 3 | Eye A blue (cathode) |
| 4 | +5 V (all anodes) |
| 5 | GND (not needed by the LEDs) |
| 6 | Eye B red (cathode) |
| 7 | Eye B green (cathode) |
| 8 | Eye B blue (cathode) |

LED current with 100 Ω: about 29 mA for red, 18 mA for green and blue, per channel, shared between however many LEDs are on that channel.

### 5. Headphone amplifier: negative supply pin is shorted to ground

U8 is a TPA6132A2. Its pin 8 (HPVSS) is the output of an internal charge pump that makes a negative supply, and needs a 1 µF capacitor to ground. On this board pin 8 is connected **directly to ground**, by a track that runs under the chip to the centre pad. The schematic has the same error, so this is a design fault, not an assembly fault.

Consequences:
- The amplifier has no negative supply, so its output cannot go below 0 V. It is designed to idle at 0 V.
- The charge pump drives into a short. Expect extra current on the 5 V rail and a warm chip. The datasheet does not say how the part behaves in this state.
- Separately, the PWM audio reaches the amplifier input with no blocking capacitor, so the input carries a DC level (half the signal swing). That DC is amplified along with the tone.

Oddly, the second fault partly hides the first: because the signal is all-positive, the output stays above 0 V and a tone may come out. But it comes out with DC on it, which flows through the headphones.

There are three ways forward. Pick one.

**Option A: try it as built (no rework on U8).**
Add a series capacitor in each headphone signal wire between H4 and the jack: 220 µF, 6.3 V or more, positive end toward the board, on OUTL and OUTR (not on ground). These keep DC out of the headphones. Then test with cheap earbuds at low volume. If the tone is clean and U8 stays merely warm, you are done. If there is no sound, heavy distortion, or U8 gets too hot to touch, send `AMP:OFF` and go to B or C. This is an experiment; I cannot tell you from the datasheet that it will work.

**Option B: repair it properly (hot-air rework).**
1. Remove U8 with hot air.
2. Cut the copper between pad 8 and the centre pad (a 0.3 mm bridge, under where the chip sits).
3. Refit U8. Add a 1 µF capacitor from pad 8 to ground (R14's grounded end is close).
4. Replace R20 and R21 (10 kΩ, 0603) with 1 µF ceramic capacitors. This blocks the DC at the input. The filter drops from two poles to one, which is fine now that the PWM runs at 31 kHz.
No output capacitors are needed after this; the amplifier output sits at 0 V as designed.

**Option C: bypass U8.**
Set `AMP_AT_BOOT` to 0 in the dsPIC firmware (the amplifier stays shut down, charge pump off). Take the audio from the volume-pot wipers, U13 pin 5 (left) and U14 pin 5 (right), plus ground, into any small headphone amplifier module that has input coupling capacitors, and wire the jack to that module. Volume control still works because the taps are after the pots.

---

## Fixed in firmware (no board work needed)

### 6. The module is an ESP32-S3, not an ESP32-WROOM-32E

U3 is `ESP32-S3-WROOM-1-N8R8` (BOM line 3, LCSC C2913201). `platformio.ini` built for `esp32dev`, a different chip family. esptool refuses to flash that image to an S3. **Fixed:** board is now `esp32-s3-devkitc-1`.

### 7. dsPIC UART pins: firmware used UART2, the board uses UART1

Net `RX0` goes to U1 pin 26 (U1RX/RF2) and net `TX0` to pin 25 (U1TX/RF3). Pins 27 and 28 (UART2), which v2.2 used and its header comment called "verified", are not connected to anything. **Fixed:** v3.0 uses UART1.

### 8. The UART link is wired transmit-to-transmit

| Net | ESP32-S3 pad | dsPIC pin | Header |
|---|---|---|---|
| `TX0` | TXD0 (GPIO43), an output by default | 25, U1TX, an output | H3 pin 5 |
| `RX0` | RXD0 (GPIO44), an input by default | 26, U1RX, an input | H3 pin 6 |

Two outputs tied together and two inputs tied together. The dsPIC30F cannot move its UART pins. The ESP32-S3 can route a UART to any pin, so **v3.0 swaps them on the ESP32 side**: it listens on GPIO43 and transmits on GPIO44.

One problem remains: the ESP32 boot ROM prints on GPIO43 at every reset, before any firmware can stop it. If the dsPIC were driving that net at the same moment the two outputs would fight. **v3.0 dsPIC firmware keeps its transmit pin high-impedance** except for the few milliseconds it takes to answer a command, and never transmits unprompted. Unknown input gets no reply at all, so noise on the shared header cannot make it talk.

A side benefit: the ESP32 can now be flashed through H3 with the dsPIC in its socket.

### 9. dsPIC was clocked at twice its rated speed

The dsPIC30F4013-20E is rated 20 MIPS at 4.5 to 5.5 V but only **10 MIPS at 3.0 to 3.6 V** (datasheet DS70138G, table 23-1). The board runs it at 3.3 V; v2.2 set the PLL for 20 MIPS. **Fixed:** v3.0 runs at 10 MIPS (10 MHz crystal, x4 PLL).

### 10. Audio generation

v2.2 produced a new sample at 8 kHz on a 256 kHz PWM with only 78 duty levels (about 6 bits). The 8 kHz sampling leaves strong images near 7.8 kHz that the 1.6 kHz filter only partly removes, and 6 bits is coarse for a pure tone. **v3.0** updates the PWM once per PWM cycle at 31.25 kHz (images are now ultrasonic), with 320 duty levels plus error feedback and a 1024-point sine table.

### 11. Volume

The AD5220 has 128 positions; v2.2 assumed 64, so it only ever reached half scale. That turned out to be harmless, because the bigger problem is the opposite one: this signal chain has far too much gain. Full scale would put over 1 V into the headphones, and the lowest pot step is already clearly audible. **v3.0** uses `VOL:0-100` on a dB scale (0.48 dB per step, top of range 12 dB below full scale). The pot sets the coarse level and the firmware trims the sine amplitude between pot steps. Volume changes ramp, the amplifier is enabled only after the output has settled, and `VOL:0` is true silence.

### 12. Smaller firmware faults fixed

- The lights now take their timing from the phase difference of the two audio oscillators, so the flash rate is exactly the beat rate and cannot drift.
- Brightness PWM restarts at each flash, so short flashes are all the same brightness.
- A manually set `BEAT` used to be overwritten by the sequencer returning to a fixed 4 Hz. The sequencer now spikes to 2x and dips to half of whatever `BEAT` is set.
- Brown-out reset was off. It is on at 2.7 V.
- Unconnected dsPIC pins were left floating; they are now driven low.
- The ESP32 firmware read VIN from a pin that cannot do it, promised a `/update` page that did not exist (it does now), and waited on a start-up banner the dsPIC can no longer send.

---

## Should fix (reliability)

### 13. No decoupling capacitors at the dsPIC

The only capacitors on the 3.3 V rail are C9 and C10 at the regulator. The dsPIC has none at its four supply pin pairs. **Fix:** solder 100 nF ceramic capacitors across the socket pins on the underside: 11-12, 20-21, 31-32, 39-40 (each pair is a neighbouring VDD and VSS).

### 14. ESP32 EN has a pull-up but no capacitor

Espressif asks for 10 kΩ plus 1 µF on EN so the chip is released after the supply is stable. The board has only the resistor (R15). **Fix:** 1 µF from H3 pin 3 (EN) to H3 pin 1 (GND).

### 15. Power supply

- Use a **regulated** 7.5 V to 9 V adapter. C8 and C12 on VIN are 10 V parts, and an unregulated "9 V" adapter can idle at 12 V or more.
- 7.5 V is better than 9 V: the 3.3 V regulator burns (VIN - 3.3 V) x about 150 mA, close to 0.9 W at 9 V in a SOT-223. It will run hot in a closed box.
- There is no reverse-polarity protection. CN1 pin 1 is ground, pin 2 is positive. Check with a meter before the first power-up.

### 16. 74HC14 input threshold

U2 and U15 run from 5 V but are driven by 3.3 V logic. A 74HC14 at 5 V switches high at typically 2.7 V, but the guaranteed worst case is about 3.5 V. Most parts will work; some may not. **If a colour never lights** (after fix 3), replace both with 74HCT14 (same DIP-14 pinout).

---

## Good to know

- **No BOOT or RESET buttons.** The older docs mention them; the board has none. Use jumpers on H3: pin 4 (IO0) to pin 1 (GND) for download mode, pin 3 (EN) to pin 1 to reset.
- **Disconnect the USB-UART adapter's TX wire (H3 pin 6) after flashing.** In normal running the ESP32 drives that net.
- **H1 is a 2x3 header, not the PICkit's 1x6 order.** Wire it: H1-1 MCLR, H1-3 VDD (3.3 V), H1-5 GND, H1-2 PGD, H1-4 PGC.
- **Programming the dsPIC at 3.3 V.** The chip's bulk-erase command is specified for 4.5 V or more. Programming normally still works at 3.3 V when code protection is off (it is). If MPLAB reports an erase or verify failure, program the chip out of the board at 5 V.
- **The lights flash during dsPIC programming** (PGC and PGD are also two LED outputs). Harmless.
- **U5, R11, R12 do nothing useful** once U5's output is disconnected.
- **Stale documentation.** Several README files described an earlier design (ESP32-WROOM-32E, MAX603, NTE941M, UART at 115200, SPI pot, battery). They have been corrected to match rev 1.1.

---

## What was verified, and how

| Item | How | Result |
|---|---|---|
| Netlist findings 1, 2, 5, 7, 8 | Pad-to-net extraction from the PCB JSON, cross-checked with the schematic PDF | Confirmed |
| U8 pin 8 routing | Track geometry in the PCB JSON | Top-layer track from pad 8 to the centre via, under the package |
| BOM part for R1 etc. | LCSC listing for C22766 | 1.2 MΩ |
| MAX8212 polarity | Datasheet | Output on when THRESH is above the reference |
| dsPIC speed limit | Datasheet table 23-1 | 10 MIPS at 3.0 to 3.6 V for the -20E |
| TPA6132A2 pin 8, gain, input limits | Datasheet SLOS597 | HPVSS needs a capacitor to ground; G0 high, G1 low is 0 dB |
| ESP32-S3 firmware | Compiled with PlatformIO for `esp32-s3-devkitc-1` | Builds without errors, 778 kB |
| dsPIC firmware logic | `firmware/dspic/test/host_test.c` on a PC | Tones 200.000 and 204.000 Hz, LED timing, parser, volume law, sequencer all pass |
| dsPIC firmware on XC16 | Not done: XC16 is not installed on this machine | Register and config names were checked against the Microchip device header |
| Anything electrical | Not done | Needs the board |
