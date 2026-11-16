# Bring-up checklist: PCB rev 1.1 + firmware v3.0

Work top to bottom. Each stage ends with a check; do not go on until it passes.
The reasons for every step are in [`hardware_review_rev1.1.md`](hardware_review_rev1.1.md).

Tools: multimeter, soldering iron, PICkit 4, 3.3 V USB-UART adapter, jumper wires.
Parts: 1x 10 kΩ, 4x 100 nF ceramic (leaded), 1x 1 µF ceramic, 6x 100 Ω (only if stage 1 says so), 2x 220 µF 6.3 V electrolytic (amplifier option A).

---

## Stage 0: board rework, power off, dsPIC out of its socket

- [ ] **U5 (MAX8212): cut or lift pin 4**, or remove the chip. Pin 4 is the last pin on the same side as pin 1.
      Check after stage 2 power-up: H3 pin 4 (IO0) reads above 3 V.
- [ ] **10 kΩ between H1 pin 1 (MCLR) and H1 pin 3 (3V3)**, on the underside.
      Check: about 10 kΩ from dsPIC socket pin 1 to socket pin 11.
- [ ] **100 nF across dsPIC socket pins 11-12, 20-21, 31-32, 39-40**, on the underside.
- [ ] **1 µF from H3 pin 3 (EN) to H3 pin 1 (GND).**

## Stage 1: resistor check, power off

- [ ] Measure across **R1**. Expect 100 Ω.
- [ ] If it reads about 1.2 MΩ: solder a 100 Ω resistor on top of each of R1, R2, R3, R16, R17, R18. Re-measure: 100 Ω.

## Stage 2: first power, dsPIC still out, nothing plugged into H4 or H5

- [ ] Supply is **regulated 7.5 V to 9 V**. Confirm polarity at CN1 with the meter: pin 1 ground, pin 2 positive.
- [ ] Power on. Measure to ground:
  - [ ] 3V3 (H3 pin 2): 3.2 to 3.4 V
  - [ ] 5V (H5 pin 4): 4.9 to 5.1 V
  - [ ] IO0 (H3 pin 4): above 3 V. **If it is near 0 V, U5 pin 4 is still connected. Stop and fix.**
  - [ ] EN (H3 pin 3): above 3 V
- [ ] Touch U6 and U4 after a minute. Warm is fine; too hot to hold a finger on means something is drawing too much.
- [ ] Power off.

## Stage 3: flash the ESP32-S3

Wire the USB-UART adapter (3.3 V logic) to H3:

| H3 pin | Label | Adapter |
|---|---|---|
| 1 | GND | GND |
| 2 | 3V3 | leave unconnected (board is powered from its own supply) |
| 3 | EN | nothing (jumper to GND by hand to reset) |
| 4 | IO0 | nothing (jumper to GND by hand for download mode) |
| 5 | TX0 | adapter **RX** |
| 6 | RX0 | adapter **TX** |

- [ ] Jumper H3 pin 4 (IO0) to pin 1 (GND). Power the board on. Remove the jumper. The chip is now in download mode.
- [ ] In `firmware/esp32/`: `pio run -e goggle -t upload`
      Expect "Chip is ESP32-S3" and "Hash of data verified".
- [ ] **Unplug the adapter's TX wire from H3 pin 6.** In normal running the ESP32 drives that pin.
- [ ] Power-cycle the board.
- [ ] Check: WiFi network **BinauralGoggle** appears. Join it (password in `main.cpp`), open `http://192.168.4.1`.
      The page loads and its top line says "dsPIC NOT RESPONDING". That is correct at this stage.

If the network does not appear: IO0 is still low (stage 2), or the upload did not complete.

## Stage 4: program the dsPIC

- [ ] Build in MPLAB X: new standalone project, device **dsPIC30F4013**, tool PICkit 4, compiler XC16.
      Add `firmware/dspic/src/dspic_firmware.c` (leave `dds_tables.h` beside it).
      Project properties, xc16-gcc, optimisation level **1**. Build. Expect no errors.
- [ ] Power off. Insert the dsPIC (notch toward pin 1 marking).
- [ ] Wire the PICkit 4 to H1 with jumper wires:

| PICkit 4 pin | Signal | H1 pin |
|---|---|---|
| 1 | MCLR | 1 |
| 2 | VDD (sense) | 3 |
| 3 | GND | 5 |
| 4 | PGD | 2 |
| 5 | PGC | 4 |

- [ ] In the project properties for the PICkit 4, power: target is powered by the board (do **not** power the target from the PICkit).
- [ ] Power the board on. "Make and Program Device". The goggle LEDs may flicker. Expect "Programming/Verify complete".
      If erase or verify fails, see "Programming the dsPIC at 3.3 V" in the review.
- [ ] Disconnect the PICkit. Power-cycle.

## Stage 5: link check

- [ ] Reload `http://192.168.4.1`. Top line: **"dsPIC online, firmware 3.0"**.
- [ ] Press Status. Expect a line starting `BEAT:4.0 CAR:200.0 VOL:30`.

If it still says not responding, press Log and look at what was sent and received.
For a closer look, put the adapter's RX on H3 pin 6 at 9600 baud to see the commands the ESP32 sends, or on H3 pin 5 to see the dsPIC's replies.
With the meter: dsPIC pin 1 (MCLR) should be at 3.3 V, and pins 11, 21, 32, 40 at 3.3 V.

## Stage 6: lights

Before plugging in the goggles, with a meter or a single LED and its own resistor:

- [ ] In the web page set mode **Solid**, colour A red. H5 pin 1 should be pulled toward ground (measure 5 V between H5 pin 4 and pin 1). Set colour A to black: the voltage goes away.
- [ ] Repeat for green (pin 2), blue (pin 3), and eye B (pins 6, 7, 8).
      If a channel never switches: 74HC14 threshold problem, see review item 16.
- [ ] Confirm the goggle LEDs are **common anode** and wired to the H5 pin map in the review. Plug in.
- [ ] Mode Alternate, 4 Hz: the eyes flash in turn, four flashes per eye per second.

**Photosensitivity warning.** Flashing light can trigger seizures, most strongly between 15 and 25 Hz. Eyes closed, and stop at once if you feel unwell.

## Stage 7: sound

Choose the amplifier option in review item 5 first.

Option A (as built):
- [ ] Fit a 220 µF capacitor in series with OUTL and with OUTR between H4 and the jack, positive end toward the board.
- [ ] Use cheap earbuds for the first test. Volume at 10 %.
- [ ] Listen: a steady low tone that seems to pulse slowly. Raise the volume gradually.
- [ ] After two minutes touch U8. Warm is acceptable. Too hot to touch, or no sound, or a harsh buzzy tone: press **Amp: OFF** and move to option B or C.
- [ ] Measure DC on the jack side of each capacitor to ground: should be about 0 V.

Option B (repaired) or C (bypassed): follow the steps in the review, then the same listening test. No output capacitors needed for B.

## Stage 8: over-the-air update check

- [ ] `http://192.168.4.1/update`, user `admin`, password as `OTA_PASS` in `main.cpp`. Upload `.pio/build/goggle/firmware.bin`.
      Or: `pio run -e goggle_ota -t upload` while joined to the goggle's WiFi.
