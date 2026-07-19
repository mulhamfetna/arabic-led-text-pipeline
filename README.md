# arabic-led-text-pipeline

**Unicode Arabic text rendering for LED dot-matrix displays.**

[![License: AGPL v3](https://img.shields.io/badge/License-AGPL_v3-blue.svg)](https://www.gnu.org/licenses/agpl-3.0)
[![DOI](https://zenodo.org/badge/1305763463.svg)](https://zenodo.org/badge/latestdoi/1305763463)

> ⚠️ **Status: design stage.** This repository currently contains the architecture and
> protocol specification only — there is no working implementation yet. See
> [Roadmap](#roadmap) for what is planned and [Limitations](#limitations) for what
> is not yet true.

## The problem

Common closed-source LED matrix controllers render text from **bitmap fonts**: one fixed
glyph image per character code. That model cannot represent Arabic.

Arabic is cursive and right-to-left. Every letter takes a different shape depending on its
neighbours — isolated, initial, medial, or final — and certain pairs must fuse into
mandatory ligatures (`لا`, `الله`). A bitmap font keyed on code points produces
disconnected, unreadable letterforms. The usual workarounds are pre-rendering fixed images
per message, or shipping a hand-drawn bitmap font that supports only one size and a limited
vocabulary.

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

Stated plainly, because none of this is built yet:

- **No implementation exists.** This repository is a specification, not working software.
- Monochrome only — the format is 1 bit per pixel; colour panels are not yet addressed.
- The MAX7219 and P10 differ in addressing, and the transposition layer is designed but untested.
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
