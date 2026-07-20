# Multi-display Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Drive WS2812B RGB strips and P10 DMD panels from the same pipeline that already drives MAX7219, without the browser or the framebuffer learning anything new about panels.

**Architecture:** Introduce a `display_driver_t` vtable so the panel type is a build-time selection rather than something `main.c` knows about. Wrap the existing 1-bit framebuffer in a `canvas_t` that can also carry 24bpp RGB. Bump the wire format to v2 with a pixel-format field. Each driver owns its own geometry, colour and current concerns; none owns text.

**Tech Stack:** ESP-IDF v5.3.2, FreeRTOS, `led_strip` (RMT) for WS2812B, SPI for MAX7219 and P10, Python 3 + Node for host-side verification.

## Global Constraints

- The MCU holds no font, no shaping tables, no text logic. A driver may know geometry, colour and current — never text.
- `framebuffer_t` keeps its exact 1-bit packing: bit 7 of byte 0 is the top-left pixel. It is the wire format stored verbatim, which is what makes the browser preview pixel-exact.
- Wire format version byte is `2`. Frames with any other version are rejected, not guessed at.
- WS2812B current clamp defaults to 2000 mA and is on by default. It is hardware protection, not a preference.
- P10 must be documented as **untested** until verified on real hardware.
- Every claim of "working" requires evidence: a build log, a host-side check, or a device log. Never assert without it.
- Branch off `dev`, PR into `dev`, squash merge. Never commit to `dev` or `main`.

---

### Task 1: Canvas type and format conversions

**Files:**
- Create: `firmware/main/canvas.h`, `firmware/main/canvas.c`
- Create: `tools/verify_canvas.py`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `framebuffer_t` from `framebuffer.h`
- Produces: `canvas_t`, `CANVAS_MONO`, `CANVAS_RGB`, `canvas_init`, `canvas_free`, `canvas_get_rgb`, `canvas_get_mono`, `canvas_from_framebuffer`

- [ ] **Step 1: Write the host-side reference for the conversions**

`tools/verify_canvas.py` — this encodes the conversion rules the C must match:

```python
#!/usr/bin/env python3
"""Reference for canvas format conversions; the C implementation must agree."""

def mono_to_rgb(bit, colour):
    """A lit mono pixel takes the frame colour; an unlit one is black."""
    return colour if bit else (0, 0, 0)

def rgb_to_mono(r, g, b, cutoff=128):
    """Rec. 601 luma, then threshold - same semantics as the browser."""
    return (0.299 * r + 0.587 * g + 0.114 * b) >= cutoff

CASES = [
    ((255, 0, 0), True,  (255, 0, 0)),
    ((255, 0, 0), False, (0, 0, 0)),
    ((0, 255, 0), True,  (0, 255, 0)),
]
for colour, bit, want in CASES:
    got = mono_to_rgb(bit, colour)
    assert got == want, f"mono->rgb {colour} {bit}: got {got} want {want}"

# Pure red luma is 76.2 -> below the 128 cutoff, so red text on a mono panel is
# DARK. This is the surprising case and the reason the cutoff is exposed.
assert rgb_to_mono(255, 0, 0) is False
assert rgb_to_mono(255, 255, 255) is True
assert rgb_to_mono(0, 255, 0) is True          # green luma 149.7
print("canvas conversion reference OK")
```

- [ ] **Step 2: Run it**

Run: `.venv/bin/python tools/verify_canvas.py`
Expected: `canvas conversion reference OK`

- [ ] **Step 3: Write `canvas.h`**

```c
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "framebuffer.h"

typedef enum { CANVAS_MONO = 0, CANVAS_RGB = 1 } canvas_fmt_t;

/*
 * What a driver is handed. Mono keeps the framebuffer's exact 1-bit packing -
 * it is the wire format stored verbatim - and carries the colour a lit pixel
 * should take. RGB is 3 bytes per pixel, row-major.
 */
typedef struct {
    uint16_t     width, height;
    canvas_fmt_t fmt;
    uint8_t     *data;
    size_t       size;
    uint8_t      colour[3];   /* mono only */
} canvas_t;

bool canvas_init(canvas_t *c, uint16_t w, uint16_t h, canvas_fmt_t fmt);
void canvas_free(canvas_t *c);
void canvas_clear(canvas_t *c);

/* Colour of pixel (x,y) whatever the underlying format. Out of bounds = black. */
void canvas_get_rgb(const canvas_t *c, int x, int y, uint8_t out[3]);

/* Lit/unlit of pixel (x,y) whatever the format. RGB uses Rec.601 luma vs cutoff. */
bool canvas_get_mono(const canvas_t *c, int x, int y, uint8_t cutoff);
```

- [ ] **Step 4: Write `canvas.c`**

```c
#include "canvas.h"
#include <stdlib.h>
#include <string.h>

bool canvas_init(canvas_t *c, uint16_t w, uint16_t h, canvas_fmt_t fmt)
{
    c->width = w; c->height = h; c->fmt = fmt;
    c->size = (fmt == CANVAS_RGB) ? (size_t)w * h * 3
                                  : (size_t)((w + 7) / 8) * h;
    c->data = calloc(1, c->size);
    c->colour[0] = 255; c->colour[1] = 255; c->colour[2] = 255;
    return c->data != NULL;
}

void canvas_free(canvas_t *c)   { free(c->data); c->data = NULL; c->size = 0; }
void canvas_clear(canvas_t *c)  { memset(c->data, 0, c->size); }

static inline size_t mono_stride(const canvas_t *c) { return (c->width + 7) / 8; }

void canvas_get_rgb(const canvas_t *c, int x, int y, uint8_t out[3])
{
    if (x < 0 || y < 0 || x >= c->width || y >= c->height) {
        out[0] = out[1] = out[2] = 0;
        return;
    }
    if (c->fmt == CANVAS_RGB) {
        const uint8_t *p = &c->data[((size_t)y * c->width + x) * 3];
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
    } else {
        const uint8_t byte = c->data[(size_t)y * mono_stride(c) + (x / 8)];
        const bool on = (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
        out[0] = on ? c->colour[0] : 0;
        out[1] = on ? c->colour[1] : 0;
        out[2] = on ? c->colour[2] : 0;
    }
}

bool canvas_get_mono(const canvas_t *c, int x, int y, uint8_t cutoff)
{
    if (x < 0 || y < 0 || x >= c->width || y >= c->height) return false;
    if (c->fmt == CANVAS_MONO) {
        const uint8_t byte = c->data[(size_t)y * mono_stride(c) + (x / 8)];
        return (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
    }
    const uint8_t *p = &c->data[((size_t)y * c->width + x) * 3];
    /* Rec. 601 luma in integer arithmetic: 77/150/29 sum to 256. */
    const uint32_t luma = (77u * p[0] + 150u * p[1] + 29u * p[2]) >> 8;
    return luma >= cutoff;
}
```

- [ ] **Step 5: Add to the build and compile**

Add `"canvas.c"` to `SRCS` in `firmware/main/CMakeLists.txt`.

Run: `cd firmware && . ~/esp/esp-idf/export.sh && idf.py build 2>&1 | grep -E "canvas.c.*(warning|error)|Project build complete"`
Expected: `Project build complete`, no warnings from `canvas.c`

- [ ] **Step 6: Commit**

```bash
git add firmware/main/canvas.* firmware/main/CMakeLists.txt tools/verify_canvas.py
git commit -m "feat(firmware): canvas type carrying mono or RGB pixels"
```

---

### Task 2: Display driver interface, with MAX7219 as first implementation

**Files:**
- Create: `firmware/main/display.h`
- Modify: `firmware/main/max7219.c`, `firmware/main/max7219.h`
- Modify: `firmware/main/CMakeLists.txt`

**Interfaces:**
- Consumes: `canvas_t` from Task 1
- Produces: `display_driver_t`, `display_get(void)` returning the selected driver

- [ ] **Step 1: Write `display.h`**

```c
#pragma once
#include <stdint.h>
#include "esp_err.h"
#include "canvas.h"

/*
 * Every panel type implements this. main.c, the HTTP endpoint, the serial
 * receiver and the scroll task talk only to these functions, so adding a panel
 * means adding one file and editing nothing else.
 *
 * A driver may know about geometry, colour and current. It must never know
 * anything about text.
 */
typedef struct {
    const char *name;
    esp_err_t (*init)(void);
    uint16_t  (*width)(void);
    uint16_t  (*height)(void);
    esp_err_t (*render)(const canvas_t *c);
    esp_err_t (*set_brightness)(uint8_t level);
    void      (*deinit)(void);
} display_driver_t;

/* The driver selected by menuconfig. */
const display_driver_t *display_get(void);
```

- [ ] **Step 2: Add a vtable to max7219.c**

Append to `firmware/main/max7219.c`:

```c
/* ─── display_driver_t implementation ─────────────────────────────────────── */

static max7219_dev_t *s_dev;

static esp_err_t drv_init(void)
{
    const max7219_config_t cfg = {
        .pin_clk   = CONFIG_MAX7219_PIN_CLK,
        .pin_din   = CONFIG_MAX7219_PIN_DIN,
        .pin_cs    = CONFIG_MAX7219_PIN_CS,
        .cols      = CONFIG_MAX7219_COLS,
        .rows      = CONFIG_MAX7219_ROWS,
        .intensity = CONFIG_MAX7219_INTENSITY,
        .mapping   = max7219_orientation(CONFIG_MAX7219_ORIENTATION < 0
                                         ? 0 : CONFIG_MAX7219_ORIENTATION),
#ifdef CONFIG_MAX7219_CHAIN_SERPENT
        .chain     = MAX7219_CHAIN_SERPENTINE,
#else
        .chain     = MAX7219_CHAIN_ROW_MAJOR,
#endif
    };
    return max7219_init(&cfg, &s_dev);
}

static uint16_t  drv_width(void)  { return max7219_width(s_dev); }
static uint16_t  drv_height(void) { return max7219_height(s_dev); }
static void      drv_deinit(void) { max7219_deinit(s_dev); s_dev = NULL; }

static esp_err_t drv_brightness(uint8_t level)
{
    return max7219_set_intensity(s_dev, level > 15 ? 15 : level);
}

/* Monochrome panel: an RGB canvas is reduced by luma. */
static esp_err_t drv_render(const canvas_t *c)
{
    return max7219_render_canvas(s_dev, c);
}

const display_driver_t max7219_display = {
    .name = "MAX7219", .init = drv_init,
    .width = drv_width, .height = drv_height,
    .render = drv_render, .set_brightness = drv_brightness,
    .deinit = drv_deinit,
};
```

- [ ] **Step 3: Add `max7219_render_canvas` reading through canvas_get_mono**

In `max7219.c`, replace the body of `pack_digit`'s pixel read so it goes through the canvas, and add:

```c
esp_err_t max7219_render_canvas(max7219_dev_t *dev, const canvas_t *c)
{
    uint8_t *row = malloc((size_t)dev->modules);
    if (!row) return ESP_ERR_NO_MEM;

    esp_err_t err = ESP_OK;
    for (int digit = 0; digit < MODULE_PX && err == ESP_OK; digit++) {
        for (int mod = 0; mod < dev->modules; mod++) {
            row[mod] = pack_digit_canvas(dev, c, mod, digit);
        }
        err = write_register(dev, (uint8_t)(REG_DIGIT0 + digit), row);
    }
    free(row);
    return err;
}
```

where `pack_digit_canvas` is `pack_digit` with `fb_get_pixel(fb, X, Y)` replaced by `canvas_get_mono(c, X, Y, 128)`.

Declare in `max7219.h`:

```c
#include "canvas.h"
esp_err_t max7219_render_canvas(max7219_dev_t *dev, const canvas_t *c);
extern const display_driver_t max7219_display;
```

- [ ] **Step 4: Build**

Run: `cd firmware && idf.py build 2>&1 | grep -E "max7219.c.*(warning|error)|Project build complete"`
Expected: `Project build complete`, no warnings

- [ ] **Step 5: Commit**

```bash
git add firmware/main/display.h firmware/main/max7219.*
git commit -m "feat(firmware): display driver interface, MAX7219 implements it"
```

---

### Task 3: Driver selection and main.c decoupling

**Files:**
- Create: `firmware/main/display.c`
- Modify: `firmware/main/main.c`
- Modify: `firmware/main/Kconfig.projbuild`

**Interfaces:**
- Consumes: `max7219_display` from Task 2
- Produces: `display_get()`

- [ ] **Step 1: Add the driver choice to Kconfig**

Insert at the top of `menu "Arabic LED text pipeline"`:

```
    choice DISPLAY_DRIVER
        prompt "Display type"
        default DISPLAY_MAX7219
        help
            Which panel is attached. Everything above the driver layer is
            identical for all three - only geometry, colour handling and
            current limiting differ.

        config DISPLAY_MAX7219
            bool "MAX7219 8x8 modules"
        config DISPLAY_WS2812B
            bool "WS2812B addressable RGB strip"
        config DISPLAY_P10
            bool "P10 DMD panel (UNTESTED)"
    endchoice
```

- [ ] **Step 2: Write `display.c`**

```c
#include "display.h"
#include "sdkconfig.h"

#if defined(CONFIG_DISPLAY_MAX7219)
extern const display_driver_t max7219_display;
#elif defined(CONFIG_DISPLAY_WS2812B)
extern const display_driver_t ws2812b_display;
#elif defined(CONFIG_DISPLAY_P10)
extern const display_driver_t p10_display;
#endif

const display_driver_t *display_get(void)
{
#if defined(CONFIG_DISPLAY_MAX7219)
    return &max7219_display;
#elif defined(CONFIG_DISPLAY_WS2812B)
    return &ws2812b_display;
#elif defined(CONFIG_DISPLAY_P10)
    return &p10_display;
#else
#  error "no display driver selected"
#endif
}
```

- [ ] **Step 3: Rewrite main.c against the interface**

Replace every `max7219_*` call and the `s_panel` variable:

```c
static const display_driver_t *s_disp;
static canvas_t s_screen;    /* exactly panel-sized */
static canvas_t s_content;   /* may be wider, for scrolling */
```

`app_main` becomes:

```c
void app_main(void)
{
    s_disp = display_get();
    ESP_ERROR_CHECK(s_disp->init());
    ESP_LOGI(TAG, "display: %s, %ux%u",
             s_disp->name, s_disp->width(), s_disp->height());

    if (!canvas_init(&s_screen, s_disp->width(), s_disp->height(), CANVAS_MONO)) {
        ESP_LOGE(TAG, "screen canvas allocation failed");
        return;
    }
    /* ... unchanged: mutex, display task, wifi, http, uart ... */
}
```

`blit_window` reads via `canvas_get_rgb`/`canvas_get_mono` and writes into `s_screen`, then calls `s_disp->render(&s_screen)`.

- [ ] **Step 4: Build and flash, confirm nothing regressed**

Run: `cd firmware && idf.py build && idf.py -p /dev/ttyUSB0 flash`
Then read the log.
Expected: `display: MAX7219, 8x8` and the Latin self-test still scrolls.

- [ ] **Step 5: Commit**

```bash
git add firmware/main/display.c firmware/main/main.c firmware/main/Kconfig.projbuild
git commit -m "refactor(firmware): main.c drives a display interface, not MAX7219"
```

---

### Task 4: Wire format v2

**Files:**
- Modify: `firmware/main/frame_rx.h`, `firmware/main/frame_rx.c`, `firmware/main/http_ui.h`, `firmware/main/http_ui.c`, `firmware/main/Kconfig.projbuild`
- Create: `tools/verify_frame_v2.py`

**Interfaces:**
- Produces: `FRAME_VERSION`, `frame_meta_t` gaining `fmt` and `colour[3]`

- [ ] **Step 1: Write the host reference for the v2 frame**

`tools/verify_frame_v2.py`:

```python
#!/usr/bin/env python3
"""Builds a v2 frame and checks its CRC against the firmware's algorithm."""
import struct, zlib

SOH, VERSION = 0x01, 2

def build(payload, w, h, fmt=0, flags=0, speed=60, colour=(255,255,255)):
    hdr = struct.pack("BBBBBB", VERSION, w, h, fmt, flags, speed) + bytes(colour)
    crc = zlib.crc32(hdr + payload) & 0xFFFFFFFF
    return bytes([SOH]) + hdr + payload + struct.pack("<I", crc)

def c_crc32(seed, data):
    crc = (~seed) & 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc >> 1) ^ (0xEDB88320 & (-(crc & 1) & 0xFFFFFFFF))) & 0xFFFFFFFF
    return (~crc) & 0xFFFFFFFF

payload = bytes([0x80, 0, 0, 0, 0, 0, 0, 0x01])
f = build(payload, 1, 8)
hdr = f[1:10]
assert c_crc32(0, hdr + payload) == struct.unpack("<I", f[-4:])[0]
assert len(f) == 1 + 9 + len(payload) + 4
print(f"v2 frame OK: {len(f)} bytes, crc {f[-4:][::-1].hex()}")
```

- [ ] **Step 2: Run it**

Run: `.venv/bin/python tools/verify_frame_v2.py`
Expected: `v2 frame OK: 22 bytes, crc ...`

- [ ] **Step 3: Update `frame_rx.h`**

```c
#define FRAME_SOH     0x01
#define FRAME_VERSION 2

/*
 * SOH | ver | w | h | fmt | flags | speed | R | G | B | payload | crc32
 *
 * ver is checked, not assumed: silently misparsing a frame is worse than
 * rejecting one. fmt 0 = 1bpp mono, 1 = 24bpp RGB. Colour applies to mono
 * frames only - it is what a lit pixel means on a colour display.
 */
```

- [ ] **Step 4: Update the receive task header parse**

In `rx_task`, read a 9-byte header and reject wrong versions:

```c
uint8_t hdr[9];   /* ver, w, h, fmt, flags, speed, R, G, B */
if (!read_exact(ctx->uart_num, hdr, sizeof(hdr), byte_timeout)) {
    ESP_LOGW(TAG, "header timeout");
    continue;
}
if (hdr[0] != FRAME_VERSION) {
    ESP_LOGW(TAG, "frame version %u, expected %u - dropping", hdr[0], FRAME_VERSION);
    continue;
}
const uint8_t w = hdr[1], h = hdr[2], fmt = hdr[3];
const size_t len = (fmt == CANVAS_RGB) ? (size_t)w * h * 3
                                       : (size_t)w * h;
```

Populate `meta.fmt` and `meta.colour`.

- [ ] **Step 5: Extend `frame_meta_t` and the HTTP endpoint**

In `http_ui.h` add to `frame_meta_t`:

```c
    uint8_t fmt;          /* CANVAS_MONO | CANVAS_RGB */
    uint8_t colour[3];    /* mono only */
```

In `http_ui.c` parse `fmt` and `colour` from the query string:

```c
if (httpd_query_key_value(query, "fmt", val, sizeof(val)) == ESP_OK) {
    meta.fmt = (uint8_t)atoi(val);
}
if (httpd_query_key_value(query, "rgb", val, sizeof(val)) == ESP_OK) {
    unsigned v = (unsigned)strtoul(val, NULL, 16);
    meta.colour[0] = (v >> 16) & 0xFF;
    meta.colour[1] = (v >> 8) & 0xFF;
    meta.colour[2] = v & 0xFF;
}
```

Raise `CONFIG_FRAME_MAX_PAYLOAD` default to `8192` with help text explaining RGB is 24× mono.

- [ ] **Step 6: Build, flash, confirm v1 frames are now rejected**

Run the old bridge (still v1) and check the log.
Expected: `frame version 1, expected 2 - dropping`

- [ ] **Step 7: Commit**

```bash
git add firmware/main/frame_rx.* firmware/main/http_ui.* firmware/main/Kconfig.projbuild tools/verify_frame_v2.py
git commit -m "feat(firmware): wire format v2 with pixel-format and colour"
```

---

### Task 5: WS2812B driver

**Files:**
- Create: `firmware/main/ws2812b.c`
- Modify: `firmware/main/Kconfig.projbuild`, `firmware/main/CMakeLists.txt`, `firmware/main/idf_component.yml`
- Create: `tools/verify_strip_layout.py`

**Interfaces:**
- Consumes: `display_driver_t`, `canvas_t`
- Produces: `ws2812b_display`

- [ ] **Step 1: Write the layout reference**

`tools/verify_strip_layout.py`:

```python
#!/usr/bin/env python3
"""Strip index for (x,y) under both layouts; the C must agree."""

def idx(x, y, w, serpentine):
    return y * w + ((w - 1 - x) if (serpentine and y % 2) else x)

W = 8
# Progressive: every row runs the same way.
assert idx(0, 0, W, False) == 0
assert idx(7, 0, W, False) == 7
assert idx(0, 1, W, False) == 8

# Serpentine: odd rows run backwards, so row 1 starts at the far end.
assert idx(0, 0, W, True) == 0
assert idx(7, 0, W, True) == 7
assert idx(7, 1, W, True) == 8     # first LED of row 1 is at x=7
assert idx(0, 1, W, True) == 15

# Every pixel must map to a distinct index, or the layout is broken.
for serp in (False, True):
    seen = {idx(x, y, W, serp) for y in range(8) for x in range(W)}
    assert len(seen) == 64, f"serpentine={serp}: {len(seen)} unique of 64"
print("strip layout OK: both layouts bijective")
```

- [ ] **Step 2: Run it**

Run: `.venv/bin/python tools/verify_strip_layout.py`
Expected: `strip layout OK: both layouts bijective`

- [ ] **Step 3: Declare the led_strip dependency**

Create `firmware/main/idf_component.yml`:

```yaml
dependencies:
  espressif/led_strip: "^2.5.3"
```

- [ ] **Step 4: Write `ws2812b.c`**

Key content — layout, current clamp, render:

```c
/*
 * WS2812B: one controller per LED, chained DIN->DOUT, 800kHz single wire,
 * 24 bits per LED in GRB order. Timing is far too tight to bit-bang, so this
 * drives the RMT peripheral through ESP-IDF's led_strip component.
 */

static int strip_index(int x, int y)
{
#ifdef CONFIG_WS2812B_SERPENTINE
    /* Odd rows run backwards - how a folded strip is normally wired, since it
       needs no return wire between rows. */
    if (y & 1) x = CONFIG_WS2812B_COLS - 1 - x;
#endif
    return y * CONFIG_WS2812B_COLS + x;
}

/*
 * Estimated current, and a clamp.
 *
 * A WS2812B draws roughly 20mA per fully-on channel, so a single LED at white
 * is about 60mA and 256 of them is 15A. A 24bpp path makes white trivially
 * reachable and the sender is not trustworthy, so the driver protects the
 * hardware itself rather than assuming good behaviour upstream.
 */
static uint32_t estimate_ma(const uint8_t *rgb, size_t leds)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < leds * 3; i++) sum += rgb[i];
    return (sum * 20u) / 255u;
}

static esp_err_t drv_render(const canvas_t *c)
{
    const size_t leds = (size_t)CONFIG_WS2812B_COLS * CONFIG_WS2812B_ROWS;

    for (int y = 0; y < CONFIG_WS2812B_ROWS; y++)
        for (int x = 0; x < CONFIG_WS2812B_COLS; x++)
            canvas_get_rgb(c, x, y, &s_rgb[strip_index(x, y) * 3]);

    const uint32_t want = estimate_ma(s_rgb, leds);
    if (want > CONFIG_WS2812B_MAX_MA) {
        const uint32_t scale = (CONFIG_WS2812B_MAX_MA * 256u) / want;
        for (size_t i = 0; i < leds * 3; i++)
            s_rgb[i] = (uint8_t)((s_rgb[i] * scale) >> 8);
        ESP_LOGW(TAG, "frame would draw ~%umA, clamped to %dmA",
                 (unsigned)want, CONFIG_WS2812B_MAX_MA);
    }

    for (size_t i = 0; i < leds; i++)
        led_strip_set_pixel(s_strip, i, s_rgb[i*3], s_rgb[i*3+1], s_rgb[i*3+2]);
    return led_strip_refresh(s_strip);
}
```

Kconfig additions: `WS2812B_PIN` (default 27), `WS2812B_COLS` (8), `WS2812B_ROWS` (8), `WS2812B_SERPENTINE` (bool, default y), `WS2812B_MAX_MA` (default 2000).

- [ ] **Step 5: Build with the driver selected**

Run: `cd firmware && idf.py menuconfig` → Display type → WS2812B, then `idf.py build`
Expected: `Project build complete`

- [ ] **Step 6: Commit**

```bash
git add firmware/main/ws2812b.c firmware/main/idf_component.yml firmware/main/Kconfig.projbuild tools/verify_strip_layout.py
git commit -m "feat(firmware): WS2812B driver with current clamp and layout mapping"
```

---

### Task 6: P10 driver (untested)

**Files:**
- Create: `firmware/main/p10.c`
- Modify: `firmware/main/Kconfig.projbuild`, `firmware/main/CMakeLists.txt`
- Modify: `docs/related-work.md` or `README.md` to record it as untested

**Interfaces:**
- Produces: `p10_display`

- [ ] **Step 1: Write `p10.c`**

```c
/*
 * P10 DMD, HUB12 interface.
 *
 * Unlike the MAX7219, a P10 panel has no refresh logic of its own: the MCU
 * must keep scanning rows or the display goes dark. This driver runs a
 * periodic timer that shifts one row group out over SPI, latches it, selects
 * the row with A/B, and pulses OE.
 *
 * UNTESTED. Written against the HUB12 specification; not verified on hardware.
 * Bring it up the same way the MAX7219 was: corner probe, derive the mapping
 * from what is observed, and only then trust it.
 */
```

Scan task at 1/4 duty, `A`/`B` row select, `LAT`, `OE` on LEDC for brightness. Pixels read via `canvas_get_mono(c, x, y, 128)`.

- [ ] **Step 2: Build with P10 selected**

Run: `idf.py menuconfig` → P10, `idf.py build`
Expected: `Project build complete`

- [ ] **Step 3: Record it as untested**

Add to README limitations: *"P10 driver is written against the HUB12 spec and has never been run on a panel."*

- [ ] **Step 4: Commit**

```bash
git add firmware/main/p10.c firmware/main/Kconfig.projbuild README.md
git commit -m "feat(firmware): P10 DMD driver (untested, spec-derived)"
```

---

### Task 7: Browser colour support

**Files:**
- Modify: `firmware/main/www/index.html`

- [ ] **Step 1: Add colour and format controls**

A colour input, and a mono/RGB choice. When the selected display is monochrome the colour control is hidden — `/panel` gains a `"colour": true|false` field so the page knows.

- [ ] **Step 2: Add the RGB packing path**

```js
/*
 * RGB skips thresholding: coverage becomes alpha against the chosen colour,
 * so anti-aliased edges survive as dimmer pixels instead of being forced to
 * on or off. The preview draws from these same bytes, so 1:1 still holds.
 */
if (fmt === "rgb") {
  const out = new Uint8Array(outW * H * 3);
  for (let i = 0, p = 0; i < outW * H; i++, p += 3) {
    const a = Math.min(255, cov[i] * gain) / 255;
    out[p]   = Math.round(col.r * a);
    out[p+1] = Math.round(col.g * a);
    out[p+2] = Math.round(col.b * a);
  }
  return out;
}
```

- [ ] **Step 3: Send fmt and colour**

`/frame?w=..&h=..&fmt=1&rgb=ff2d20&mode=..&speed=..&dir=..`

- [ ] **Step 4: Syntax check**

Run: `node -e "..."` extracting the script and calling `new Function` on it
Expected: `JS syntax OK`

- [ ] **Step 5: Commit**

```bash
git add firmware/main/www/index.html
git commit -m "feat(ui): colour picker and RGB frame path"
```

---

### Task 8: Debug bridge v2 and end-to-end verification

**Files:**
- Modify: `host/debug_bridge.py`

- [ ] **Step 1: Update the frame builder to v2**

```python
SOH, VERSION = 0x01, 2

def build_frame(payload, w, h, fmt=0, scroll=False, speed_ms=60,
                rightward=False, colour=(255, 255, 255)):
    flags = (FLAG_SCROLL if scroll else 0) | (FLAG_RIGHTWARD if rightward else 0)
    header = struct.pack("BBBBBB", VERSION, w, h, fmt, flags,
                         max(10, min(255, speed_ms))) + bytes(colour)
    crc = zlib.crc32(header + payload) & 0xFFFFFFFF
    return bytes([SOH]) + header + payload + struct.pack("<I", crc)
```

- [ ] **Step 2: Forward fmt and colour from the query string**

- [ ] **Step 3: Verify a mono frame still renders on MAX7219**

Send the letter F; device must log `rendered frame 8x8`.

- [ ] **Step 4: Verify an RGB frame is accepted**

Send an 8×8 RGB frame (192 bytes); device must log the frame and, on WS2812B, either render or report a clamp.

- [ ] **Step 5: Commit**

```bash
git add host/debug_bridge.py
git commit -m "feat(host): debug bridge speaks wire format v2"
```

---

## Self-review

**Spec coverage:** driver interface (T2, T3), canvas (T1), format v2 (T4), WS2812B with clamp and layouts (T5), P10 untested (T6), browser colour (T7), bridge (T8). All sections covered.

**Type consistency:** `canvas_t`, `canvas_get_rgb`, `canvas_get_mono`, `display_driver_t`, `display_get`, `frame_meta_t.fmt`, `frame_meta_t.colour` used identically across tasks.

**Known gap:** the mono→RGB case where red text reads dark on a mono panel (luma 76 < 128) is surfaced in Task 1's reference but is a *property*, not a bug — worth mentioning in the UI so it isn't reported as one.
