# MAX7219 bring-up

## Why this step exists

The MAX7219 drives eight "digit" registers and eight "segment" lines. On a
seven-segment display that mapping is fixed, but on an 8×8 LED matrix board it
depends entirely on how the module maker wired the grid — and generic boards
(FC-16, Parola, ICStation, unbranded 4-in-1) disagree with each other.

Nothing can be read back over SPI: MAX7219 has no MISO. So the mapping cannot be
autodetected, only observed. That is what the bring-up pattern is for.

## Wiring

| MAX7219 | ESP32 | Note |
|---|---|---|
| VCC | 5V (VIN) | Not 3V3 — the LEDs are dim and unreliable at 3.3V |
| GND | GND | Must be common with the ESP32 |
| DIN | GPIO23 | VSPI MOSI |
| CS  | GPIO5  | VSPI CS |
| CLK | GPIO18 | VSPI SCK |

Change any of these under `idf.py menuconfig` → *Arabic LED text pipeline* →
*MAX7219 wiring*. The same menu sets how many 8×8 modules are cascaded
(default 4, i.e. a 32×8 "4-in-1" board).

> **Power:** a fully lit 4-module board can pull well over 500mA. USB bus power
> is enough for the bring-up pattern at the default intensity of 2, but expect
> brownouts and resets if you raise intensity with everything lit. Use an
> external 5V supply, grounds tied together, for anything sustained.

## Running the pattern

Flashed with the mapping left at *Not yet determined*, the firmware boots
straight into a loop that cycles all four candidate mappings:

```bash
cd firmware
idf.py -p /dev/ttyUSB0 flash monitor
```

For each candidate it draws four shapes, logging what each one *should* look
like, then lights each module in turn:

| Step | Correct appearance |
|---|---|
| single pixel | one LED, top-left corner of the leftmost module |
| horizontal line | top edge lit, full width of the panel |
| vertical line | left edge lit, 8px tall |
| letter L | upright L at the far left |
| module walk | one module lit at a time, left to right |

Every shape is asymmetric on purpose. A symmetric one — a full square, a
centred dot — looks identical under several mappings and proves nothing.

## Reading the result

Watch which of the four candidates draws the shapes as described, then set it:

```bash
idf.py menuconfig
#  → Arabic LED text pipeline → Module pixel mapping
idf.py -p /dev/ttyUSB0 flash
```

The firmware then boots into frame-receive mode instead of the pattern loop.

Common outcomes:

- **Everything looks right under `ROW`** — a row-addressed board, the simplest case.
- **The L is rotated 90°** — you want a `COL` mapping.
- **The L is mirrored left-to-right** — you want a `_REV` variant.
- **Shapes are correct but modules light right-to-left** — the chain runs the
  other way; the panel is physically reversed relative to DIN. Either flip the
  board or file it as a follow-up (a chain-reversal option is not implemented yet).
- **Nothing lights at all** — check power and grounds before suspecting the
  mapping. Also confirm CS: a floating CS shows as a dark or randomly-lit panel.

## What comes next

Once the mapping is set, the device listens for frames on UART0 at 115200 baud.
Logs still go out on TX while frames arrive on RX, so a single USB cable serves
both — the log noise cannot corrupt inbound frames.
