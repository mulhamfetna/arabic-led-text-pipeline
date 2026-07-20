# Wiring — 74HC595 matrix

Building the scanned matrix by hand, instead of buying a MAX7219 module or a P10 panel.

You are building the same circuit a P10 panel contains: shift registers holding the column data,
a transistor array sinking the row current, and the LEDs between them. The advantage is any
geometry you like; the cost is that you supply the current limiting a MAX7219 has built in.

---

## Parts

For a single 8×8 panel:

| Part | Qty | Purpose |
|---|---|---|
| **74HCT**595 shift register | 2 | one for columns, one for rows — see [logic levels](#power) for why HCT rather than HC |
| ULN2803A darlington array | 1 | sinks the row current |
| 8×8 LED matrix, **common row cathode** | 1 | the display |
| Resistors — value calculated below | 8 | one per column |
| 0.1 µF ceramic capacitor | 2–3 | one across each chip's supply |

Scaling: **one more 595 per 8 columns**, and **one more 595 + one more ULN2803 per 8 rows**.

---

## The circuit

```
             ┌─────────────────────────────────────────────┐
  ESP32      │                                             │
             │   ┌──────────┐        ┌──────────┐          │
  GPIO13 ────┼──▶│ DS       │        │ DS       │          │
  (DATA)     │   │   595    │ Q7'───▶│   595    │          │
  GPIO14 ────┼──▶│  ROWS    │        │ COLUMNS  │          │
  (CLK)      │   │          │        │          │          │
  GPIO27 ────┼──▶│ ST_CP    │        │ ST_CP    │          │
  (LATCH)    │   └────┬─────┘        └────┬─────┘          │
  GPIO33 ────┼───────────────────────────▶│ OE            │
  (OE)       │        │                   │                │
             │        ▼                   ▼                │
             │   ┌─────────┐         ┌─────────┐           │
             │   │ ULN2803 │         │ 8 × R   │           │
             │   └────┬────┘         └────┬────┘           │
             │        │ rows (cathodes)   │ cols (anodes)  │
             │        ▼                   ▼                │
             │      ┌───────────────────────┐              │
             │      │    8×8 LED matrix     │              │
             │      └───────────────────────┘              │
             └─────────────────────────────────────────────┘
```

**Chain order matters.** The ESP32's data line goes to the **row** register first, whose `Q7'`
feeds the **column** register. The firmware transmits column bytes before row bytes to match,
because the byte sent first travels furthest along the chain.

---

## Pin connections

### ESP32 → first 74HC595

| ESP32 | 595 pin | Name |
|---|---|---|
| GPIO13 | 14 | `DS` / `SER` — serial data |
| GPIO14 | 11 | `SH_CP` / `SRCLK` — shift clock |
| GPIO27 | 12 | `ST_CP` / `RCLK` — latch |
| GPIO33 | 13 | `OE` — output enable, active low |
| 5 V | 16 | `VCC` |
| GND | 8 | `GND` |

### Every 74HC595, without exception

| 595 pin | Connect to | Why |
|---|---|---|
| 10 (`MR`/`SRCLR`) | **VCC** | Master reset is **active low**. Left floating, the register clears at random and the display flickers or blanks. This is the single most common omission. |
| 13 (`OE`) | GPIO33 | Shared across all column registers so brightness applies uniformly |
| 8, 16 | GND, VCC | Plus a 0.1 µF capacitor between them, close to the chip |

### Chaining

`Q7'` (pin 9) of one register → `DS` (pin 14) of the next. `SH_CP`, `ST_CP` and `OE` are **shared
by every register** — wire them in parallel.

### Rows → ULN2803

The row register's outputs drive the ULN2803's inputs, and the ULN2803's outputs pull the matrix
rows to ground.

| ULN2803 | Connect to |
|---|---|
| 1–8 (inputs) | 595 row outputs `Q0`–`Q7` |
| 18–11 (outputs) | matrix row cathodes — note the **reverse order**: input 1 → output 18 |
| 9 | GND |
| 10 (`COM`) | leave unconnected for LEDs |

---

## Current — read this before choosing resistors

Two separate limits, and the second one catches almost everybody.

### Rows: why the ULN2803 exists

A row carries the current of every lit LED in it — up to **8 × 20 mA = 160 mA**. A 74HC595 pin is
rated **35 mA**. Connecting rows directly destroys the chip. The ULN2803 exists solely to sink that
current instead.

### Columns: the limit nobody mentions

Each column drives only **one** LED at a time, because only one row is lit — so per-pin current is
fine.

But the 74HC595's **package** limit is **70 mA total across all outputs**, and eight columns at
20 mA each is **160 mA**. A row of eight lit pixels exceeds the package rating even though no
single pin does.

So the resistors must limit total package current, not just per-pin:

```
R = (5 V − 2 V) / I          2 V ≈ red LED forward voltage

  390 Ω → 7.7 mA per column → 61.5 mA package   ✅ under 70 mA
  330 Ω → 9.1 mA per column → 72.7 mA package   ⚠  marginally over
  150 Ω → 20  mA per column → 160  mA package   ❌ more than double
```

**Use 390 Ω** for a plain two-chip build. The panel is dimmer than a MAX7219 module — that chip
uses constant-current drivers rated for the whole package, which is exactly what you are giving up.

**If you want it brighter**, add a source driver such as a **UDN2981** or **TD62783** on the column
side, mirroring the ULN2803 on the rows. Then 150 Ω and full brightness are fine, because the 595
only drives the driver's inputs rather than the LEDs.

### And remember the duty cycle

Only one row is lit at a time, so each LED is on 1/8 of the time on an 8-row panel — and 1/32 on a
32-row one. Perceived brightness falls with height regardless of resistor choice. See
[Scaling](#scaling).

---

## Scaling

Set `HC595_COL_REGS` and `HC595_ROW_REGS` in `idf.py menuconfig`.

### Wider — free

Add one 595 per 8 columns, chained after the existing column register. Every column in a row lights
at the same instant, so **a wider panel is no dimmer**.

```
  [595 rows] ─▶ [595 cols] ─▶ [595 cols] ─▶ [595 cols]
                    8            16            24  columns
```

### Taller — costs brightness

Add one 595 **and one ULN2803** per 8 rows.

| Rows | Duty per LED | Relative brightness |
|---|---|---|
| 8 | 1/8 | 100% |
| 16 | 1/16 | 50% |
| 24 | 1/24 | 33% |
| 32 | 1/32 | 25% |

This is physics: one row lit at a time means doubling the height halves each LED's on-time. No
firmware setting recovers it. Past about 32 rows you are building a dim display, and the real
remedy is splitting the panel into two halves scanned in parallel — not supported here.

### Square

Both together. `2 × 2` registers gives 16×16; `4 × 4` gives 32×32 at 1/32 duty, which will be
noticeably dim.

---

## Power

- **Logic levels — pick one of these, do not skip it.**

  A **74HC**595 running at 5 V needs `VIH = 0.7 × VCC = 3.5 V` to read a reliable logic 1. The
  ESP32 outputs **3.3 V**. That is *below spec* — it often appears to work on the bench and then
  fails intermittently, or when the chip warms up, which is a miserable fault to chase.

  | Option | Why |
  |---|---|
  | **74HCT595 at 5 V** ← recommended | TTL-compatible inputs, `VIH = 2.0 V`. Drop-in, same pinout |
  | 74HC595 at **3.3 V** | `VIH` becomes 2.31 V, so 3.3 V logic is fine. LEDs are dimmer |
  | 74HC595 at 5 V + level shifter | Works, but that is a whole extra chip to avoid buying the HCT |

  The ULN2803 is unaffected either way — its inputs are happy from 2.4 V.
- **Tie all grounds together** — ESP32, chips, LED supply. Voltage is a difference between two
  points, and without a shared reference the data lines mean nothing.
- **Budget the current**: `columns × per-column mA`. A 32-wide panel at 8 mA is 256 mA when a whole
  row is lit. USB can supply that; a 32×32 build wants its own supply.
- **One 0.1 µF capacitor per chip**, between VCC and GND, physically close to the chip. Shift
  registers switch fast and the current spikes cause misclocking without them.

---

## Bring-up

Same order as the MAX7219, and for the same reason — prove one layer at a time.

1. **Before connecting the matrix**, power the chips and check `OE` and `MR` sit at the right
   levels. `MR` floating is the classic fault.
2. Select **74HC595 matrix** under `idf.py menuconfig` → Display type, set your register counts.
3. Flash and watch the log:
   ```
   scan_matrix: 74HC595: 8x8 px, 8 phases, 1/8 duty, 100Hz refresh
   ```
4. The Latin self-test scrolls. **If English does not render correctly, Arabic never will.**
5. If the image is mirrored or rotated, the wiring order of the outputs is reversed — check
   `Q0`–`Q7` against the matrix pins before suspecting the firmware.

---

## Common faults

| Symptom | Cause |
|---|---|
| Random flicker, blanking | `MR` (pin 10) left floating — tie it to VCC |
| Nothing lights | `OE` (pin 13) high, or rows wired to the 595 instead of the ULN2803 |
| Whole panel dim | Expected — see the duty-cycle table, or add a column source driver |
| One row much brighter | That row is being selected twice; check the one-hot row wiring |
| Image shifted vertically by 8 | Row register order swapped in the chain |
| A smear rather than text | Chain order reversed — data must reach the **row** register first |
| Works at 8 rows, garbage at 16 | Second ULN2803 missing or its ground not shared |
| Chip hot to the touch | Package current exceeded — resistors too small. Recalculate |
| Works on the bench, fails later or when warm | 74HC at 5 V driven from 3.3 V logic — out of spec. Use 74HCT, or run at 3.3 V |

---

## Why choose this over a MAX7219 or P10

| | MAX7219 module | P10 panel | This |
|---|---|---|---|
| Chips to wire | 0 (module) | 0 (panel) | 3+ |
| Current limiting | built in | built in | **your resistors** |
| Multiplexing | in hardware | firmware | firmware |
| Geometry | 8×8 multiples | fixed 32×16 | **anything** |
| Brightness | good | good | lower, unless you add a source driver |

Choose this when you want geometry the other two do not sell, or when the point is to have built it.
If you just want text on a panel, buy a module — this route is more work for a dimmer result.

> **Status: untested.** This driver has never been run on hardware. The firmware builds and the
> chain ordering and row walk are verified in simulation, but nothing here has lit a real LED yet.
