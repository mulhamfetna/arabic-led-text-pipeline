# Scanned matrix core — P10 and 74HC595 as configurations of one driver

**Date:** 2026-07-20
**Status:** approved, not yet implemented

## The observation that shaped this

A P10 panel is not a different kind of device from a hand-built 74HC595 matrix. It **is** that
circuit, pre-assembled: shift registers holding the column data, a decoder and darlington sinks
selecting and driving the rows, and the LEDs. HUB12 merely exposes the signals — `R` is serial
data, `CLK` the clock, `LAT` the latch, `A`/`B` the row select, `OE` the blanking.

Writing two drivers would produce two copies of the same scanner, which would then drift apart.

## What actually differs

Only the **row-selection mechanism**:

| | P10 | DIY 74HC595 |
|---|---|---|
| Row select | `A`/`B` pins, 2 bits decoded | one-hot bits in the shift chain |
| Rows lit simultaneously | 4 (rows p, p+4, p+8, p+12) | 1 |
| Scan ratio | fixed 1/4 | 1/N, chosen by geometry |
| Column data | serial, one line | serial, same line |
| Latch, OE, blanking, timing | identical | identical |

Everything else — the refresh timer, the shadow buffer, the shift/latch/select/unblank sequence,
the geometry mapping and the brightness PWM — is common.

## Design

### 1. A shared scanned-matrix core

`scan_matrix.c` owns everything both panels share:

- a periodic timer that walks scan phases continuously, because neither panel refreshes itself
- a shadow buffer written by `render()` under a critical section, read by the timer
- the per-phase sequence: blank → shift → latch → select row → unblank
- `OE` on an LEDC channel, so brightness is a duty cycle
- conversion from the canvas via `canvas_get_mono()`

It is parameterised by a `row_select_t` strategy:

```c
typedef enum {
    ROW_SELECT_BINARY,      /* A/B address lines, or a 74HC138 decoder */
    ROW_SELECT_SHIFT_REG,   /* one-hot bit within the serial chain     */
} row_select_t;
```

### 2. Two thin configurations

`p10.c` and `hc595.c` become configuration and a vtable over the core, not scanners in their own
right. Each supplies pins, geometry, scan ratio and row-select strategy, then exposes a
`display_driver_t`.

### 3. Chain ordering

For `ROW_SELECT_SHIFT_REG` the chain is `ESP32 → [row regs] → [column regs]`.

Data shifts through, so the byte transmitted **first** lands in the **last** chip. Column bytes are
therefore sent before row bytes. This is the same inversion the MAX7219 cascade already has, and
the same one that is easy to get backwards.

### 4. Scaling, and the limit that is physical

```
width  = col_registers × 8
height = row_registers × 8
```

Both configurable, so 2×2 registers gives a 16×16 square.

**Horizontal scaling is free; vertical scaling costs brightness.** Only one row is lit at a time,
so doubling the height halves each LED's duty cycle:

| Rows | Duty | Relative brightness |
|---|---|---|
| 8 | 1/8 | 100% |
| 16 | 1/16 | 50% |
| 32 | 1/32 | 25% |

This is physics, not a software limit. Past roughly 32 rows the display is dim regardless of
firmware, and the real remedy is a second scan chain driving two halves in parallel — out of scope
here, but the ceiling must be documented rather than discovered after buying parts.

### 5. Current — the hardware requirement

A 74HC595 pin is rated **35 mA**, and **70 mA per package**. A row of 8 lit LEDs at 20 mA each is
**160 mA**. Driving rows straight from a 595 damages it.

Row-select outputs must therefore feed a **ULN2803** or discrete transistors that sink the actual
current. The wiring documentation must state this as a requirement, not a suggestion.

Column outputs need one **current-limiting resistor per column**, which a MAX7219 provides
internally and a 595 does not.

### 6. User-facing selection

`HC595` joins the existing `DISPLAY_DRIVER` choice alongside MAX7219, WS2812B and P10, so the panel
type stays a single `menuconfig` selection.

## Payoff for P10

The P10 driver is currently untested because no panel is available. Once the 595 build is verified
on hardware, the shared core is proven and only the `ROW_SELECT_BINARY` strategy remains unverified
— turning "entirely untested" into "one parameter untested."

## What can be verified without hardware

- bit ordering and chain inversion, against a host-side reference
- the one-hot row walk covering every row exactly once
- geometry mapping being bijective
- all four driver selections building

## What cannot

Whether either panel physically lights correctly. Both remain labelled untested until run on
hardware, following the corner-probe bring-up used for the MAX7219.

## Out of scope

Parallel scan chains for large panels, colour, per-panel gamma, and daisy-chaining mixed panel
types.
