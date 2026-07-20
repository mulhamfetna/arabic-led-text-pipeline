# Multi-display support — WS2812B and P10 alongside MAX7219

**Date:** 2026-07-20
**Status:** approved, not yet implemented

## Problem

The system drives one panel type. `main.c` calls `max7219_*` directly, so adding a display means
editing application logic rather than adding a driver.

Two new targets are wanted:

- **WS2812B addressable RGB strips** — one controller per LED, chained DIN→DOUT, single-wire at
  800 kHz, 24 bits per LED in GRB order.
- **P10 DMD panels** — 32×16 monochrome, HUB12 interface, requiring a continuous refresh scan
  driven by the MCU.

WS2812B breaks an assumption the wire format rests on: the payload is 1 bit per pixel, and these
strips are full colour.

## Constraint that must survive

The browser shapes Arabic and sends packed pixels; the MCU holds no font, no shaping tables, and no
text logic. Every decision below preserves that. A display driver may know about geometry, colour
and current — never about text.

## Design

### 1. Display driver interface

The framebuffer is already panel-agnostic (zero driver references). The coupling is confined to
`main.c`. Introduce:

```c
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    uint16_t  (*width)(void);
    uint16_t  (*height)(void);
    esp_err_t (*render)(const canvas_t *c);
    esp_err_t (*set_brightness)(uint8_t level);
    void      (*deinit)(void);
} display_driver_t;
```

`main.c`, `http_ui.c`, `frame_rx.c` and the scroll task talk only to this. One driver is selected at
build time via `menuconfig`. A fourth panel type later means one new file and no edits elsewhere.

### 2. Canvas replaces framebuffer at the driver boundary

`framebuffer_t` is 1-bit and stays exactly as it is — it is the wire format stored verbatim, which
is what makes the browser preview pixel-exact.

A `canvas_t` wraps it plus optional colour:

```c
typedef struct {
    uint16_t width, height;
    uint8_t  fmt;          /* CANVAS_MONO | CANVAS_RGB */
    uint8_t *data;         /* mono: 1bpp packed; rgb: 3 bytes/pixel */
    uint8_t  colour[3];    /* mono only: what a lit pixel means */
} canvas_t;
```

Mono canvases keep the existing packing and the existing memory cost. RGB is opt-in per frame.

### 3. Wire format v2

```
SOH(0x01) | ver | w_bytes | h_rows | fmt | flags | speed | payload | crc32
```

- `ver` = 2. **Breaking change.** Old host tooling must be updated; a version field is added
  because silently misparsing a frame is far worse than rejecting one.
- `fmt` = 0 (1bpp mono) or 1 (24bpp RGB). GRB byte order is a WS2812B driver concern, not a format
  concern — the wire is RGB.
- `w_bytes` remains width in **bytes** for mono. For RGB, width in pixels is carried directly and
  payload length is `w × h × 3`.

Conversions are defined in both directions so any frame renders on any display:

| Frame → Display | Behaviour |
|---|---|
| mono → MAX7219 / P10 | direct |
| mono → WS2812B | lit pixels take the header colour |
| RGB → WS2812B | direct |
| RGB → MAX7219 / P10 | Rec. 601 luma, then threshold |

`FRAME_MAX_PAYLOAD` rises to accommodate RGB scroll buffers: a 200 px phrase at 8 rows is 4800
bytes against 200 for mono.

### 4. Geometry mapping, unified

Serpentine and progressive layouts are supported for both the WS2812B matrix and the MAX7219 grid,
sharing one mapping function. Layout is configuration, not code.

For a strip, `(x, y) → strip index`:

- progressive: `y * width + x`
- serpentine: `y * width + (y odd ? width - 1 - x : x)`

A single straight line is the degenerate case where `height == 1`.

### 5. Power safety — WS2812B only

256 LEDs at full white draw roughly 15 A. A 24bpp path makes white trivially reachable, and the
sender is not trustworthy.

The WS2812B driver estimates current per frame as approximately
`Σ(r + g + b) / 255 × 20 mA` per LED, and if the total exceeds a configured budget (default 2000 mA,
safe on USB) scales the whole frame down proportionally before emitting it. Clamping is logged, so
a dimmed display has a visible cause rather than appearing broken.

This is a firmware safety property, not a preference: it protects hardware regardless of what the
browser sends.

### 6. P10 driver

Unlike MAX7219, P10 has no refresh logic of its own — the MCU must scan rows continuously or the
panel goes dark. The driver runs a timer-driven refresh task shifting row data out over SPI, with
A/B row-select lines, `LAT` to latch, and `OE` for brightness via PWM.

**This driver is written against the HUB12 specification and is untested.** It must not be claimed
as working until verified on real hardware, following the same corner-probe bring-up used for
MAX7219.

### 7. Browser changes

The page gains a colour control and a mono/RGB choice. The mono path is untouched: shape → render →
supersample → box filter → normalise → threshold → pack.

The RGB path skips thresholding and emits colour pixels directly. The 1:1 preview guarantee holds in
both: the preview draws from the same bytes that are sent.

## What does not change

- Browser-side Arabic shaping
- The framebuffer's 1-bit packing and its exact correspondence to the panel
- Scroll, direction, speed, threshold semantics
- The captive portal, web UI structure, and serial debug bridge (beyond the format version bump)

## Risks

| Risk | Mitigation |
|---|---|
| RGB payloads exhaust MCU RAM on long scrolls | Raise the payload cap deliberately; reject oversized frames with a clear error rather than crashing |
| WS2812B timing is strict; bit-banging fails | Use the RMT peripheral via ESP-IDF's `led_strip` component |
| Full-white current damages hardware | Firmware current estimator and clamp, on by default |
| P10 claimed working when untested | Documented as untested; bring-up procedure specified |
| Format v2 breaks the debug bridge | Bridge updated in the same change; version field makes mismatches explicit |

## Out of scope

Per-pixel animation from the host, video, multi-panel chaining across display types, and gamma
correction. Each is additive later and none is required for correct Arabic text.
