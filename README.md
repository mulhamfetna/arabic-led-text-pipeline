# arabic-led-text-pipeline

**Unicode Arabic text rendering for LED dot-matrix displays.**

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![DOI](https://zenodo.org/badge/1305763463.svg)](https://zenodo.org/badge/latestdoi/1305763463)

> ⚠️ **Status: early. Latin works on hardware; Arabic does not exist yet.**
>
> The ESP32 firmware runs: it drives a MAX7219 cascade, serves its own WiFi access point and
> web UI, and renders scrolling Latin text from an embedded 5×7 font as a hardware self-test.
> **The Arabic path — the actual point of the project — is not implemented.** See
> [Limitations](#limitations).

## The problem

Arabic is cursive and right-to-left. Every letter takes a different shape depending on its
neighbours — isolated, initial, medial, or final — and certain pairs must fuse into mandatory
ligatures (`لا`, `الله`). A bitmap font keyed on code points produces disconnected, unreadable
letterforms.

**The gap is specific, and narrower than "nobody has solved Arabic on embedded".** Arabic shaping
and Unicode bidi already exist in open embedded C: [LVGL](https://github.com/lvgl/lvgl) (MIT) does
both at runtime on a microcontroller, and standalone Arduino reshapers do positional substitution
with 8×8 fonts. Credit where due — see [`docs/related-work.md`](docs/related-work.md).

What is missing is Arabic on **LED dot-matrix panels** specifically:

- **u8g2** drives MAX7219 and ships Persian fonts, but its maintainer has declined contextual
  shaping since 2018 — *"I think this is not something for u8g2"* — while disconnected-letter
  reports have stayed open from 2018 through 2024.
- **ESPEasy's P104** (MD_Parola/MAX7219) ships an Arabic font whose own documentation says it
  "should not be used to 'translate' normal text to Arabic", because the fonts are 8-bit codepage
  rather than Unicode.
- **LVGL**, which does have shaping, targets anti-aliased TFT GUIs — not 1-bit panels at 8–16px.

So: **an open, Unicode-correct (shaping + bidi) text pipeline for 1-bit LED matrices at 8–16px.**
That is the gap.

> On the commercial side, Arabic is not a documented *controller-firmware* capability — whatever
> shaping happens, happens upstream in closed Windows authoring software. That means the industry
> already uses a host-shapes-then-ships-bitmap split, the same architecture as this project. It
> validates the approach rather than distinguishing it.

## The approach

Do the hard part on the host, and send the panel nothing but pixels.

```
Arabic text  →  shaping  →  rasterise  →  fit  →  threshold  →  pack  →  frame  →  MCU  →  LEDs
              (HarfBuzz)     (TTF)      (letterbox)  (1-bit)   (8px/byte)  (+CRC)
```

The host application performs contextual shaping with HarfBuzz, rasterises the shaped glyph
run with a real TrueType font (Amiri, Lateef) at 4× supersampling, letterboxes it to the exact
panel dimensions, thresholds to 1 bit, and packs 8 horizontal pixels per byte. The
microcontroller receives a framebuffer and a header, and does nothing but drive the scan.

This split is the core design decision:

| | Host (desktop/mobile app) | Microcontroller (ESP32) |
|---|---|---|
| Arabic shaping | ✅ | ❌ |
| Font storage | ✅ | ❌ |
| Rasterisation | ✅ | ❌ |
| Framebuffer + panel scan | ❌ | ✅ |

Because the MCU holds no font and no text logic, **any** microcontroller can implement the
receiver from the wire format alone, and adding a new font or font size costs nothing on the
device.

## Using it from a phone

The ESP32 runs its own WiFi access point and web server, so no router, no app, and no internet
are involved:

1. Power the board. It advertises an open network named **`ARABIC-LED`**.
2. Join it from the phone. A captive-portal DNS answers every lookup with the device, so the
   phone should offer the page by itself; otherwise open **`http://192.168.4.1/`**.
3. Type Arabic, watch the live preview, press send.

**The shaping happens in the phone's browser.** Canvas `fillText` already drives the platform's
full text engine — contextual forms and ligatures included — so the page renders, thresholds and
packs the pixels, and POSTs finished bytes. The ESP32 still stores no font and runs no text logic,
exactly as the architecture requires; the browser simply *is* the host.

> Bluetooth cannot do this. A phone browser cannot open a page over BLE — Web Bluetooth needs a
> page already served over HTTPS from the internet, and iOS Safari does not support it at all.
> WiFi SoftAP is the only approach that works with a stock phone and no installed app.

See [`docs/wiring.md`](docs/wiring.md) for hookup and [`docs/bringup.md`](docs/bringup.md) for
first-light.

## Wire format

```
SOH(0x01) | W_bytes | H_rows | [payload] | CRC32
```

For a 64×32 panel: 8 bytes/row × 32 rows = **256 bytes** of payload. Bit 7 of byte 0 is the
top-left pixel, and the mapping onto physical LEDs is 1:1 — which is what allows the host
preview to be pixel-exact rather than approximate.

## Hardware targets

| Target | Status | Notes |
|---|---|---|
| MAX7219 8×8 cascade | bring-up target | Cheap; column-addressed tiles, so the driver layer transposes |
| P10-1R-1S (32×16 red) ×2 → 64×32 | primary goal | Row-major, matches the native packing |
| Addressable / multicolour LED chains | future | Would extend the format beyond 1 bit per pixel |

Everything up to and including bit-packing is panel-agnostic; only the driver layer and the
header dimensions know which panel is attached.

## Roadmap

Tracked as [epics and issues](../../issues). Broadly: host shaping and render pipeline →
wire protocol and CRC → ESP32 receiver and MAX7219 driver → P10 driver → desktop app with
1:1 preview → WiFi transport and message playlist.

## Limitations

Stated plainly:

- **Whether browser-side shaping is novel is unverified.** A survey found no confirmation either
  way that canvas/WASM shaping emitting a packed 1-bit framebuffer to an MCU has been done before.
  Treat it as a reasonable architecture, not a claimed first.
- **No published work was found** on Arabic legibility at 8–16px, so whether a purpose-designed
  low-resolution glyph set is needed remains an open question.
- **The Arabic path is unimplemented.** The web UI shapes text via canvas, but has not been
  validated against real Arabic on hardware. Only the Latin self-test is proven end to end.
- Monochrome only — the format is 1 bit per pixel; colour panels are not yet addressed.
- MAX7219 works; **P10 is not implemented**. The transposition layer is written and
  orientation-verified on the host, but P10's row-major addressing is untested.
- Rendering quality at very small pixel heights (7–16 px) is the hardest open problem, and
  the threshold value is expected to need per-font tuning.

## Licence

[GNU AGPL-3.0-or-later](LICENSE). If you run a modified version as a network service, you must
make your source available to its users.

## Citation

If you use this work in research, please cite it. See [`CITATION.cff`](CITATION.cff), or use
GitHub's **“Cite this repository”** button.

A Zenodo DOI will be minted on the first tagged release.

## Author

**Mulham Fetna** — [ORCID 0009-0006-4432-798X](https://orcid.org/0009-0006-4432-798X) ·
[contact@mulhamfetna.com](mailto:contact@mulhamfetna.com)
