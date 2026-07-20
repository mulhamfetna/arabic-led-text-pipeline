# Related work and prior art

*Compiled 2026-07-19 from a multi-source survey with adversarial verification, then spot-checked
against primary sources on 2026-07-20.*

**Verification status.** The load-bearing claims — the ones the project's positioning actually
rests on — were re-checked by hand against the original source, not taken on the survey's word:

| Claim | Primary source | Result |
|---|---|---|
| LVGL implements Arabic shaping | `lv_text_ap.c` on GitHub | ✅ **Verified** — every named identifier exists |
| u8g2 maintainer declined shaping | GitHub API, issues #529/#640/#1995 | ✅ **Verified verbatim** |
| ESPEasy Arabic font can't render Arabic text | `P104.rst` source | ✅ **Verified verbatim** |
| Memory footprint blocked shaping in u8g2 | GitHub API, issue #529 | ⚠️ **Survey was WRONG** — see §3 |

That last row matters: the automated pass refuted the memory-footprint claim 0–3, but the primary
source states it plainly. **A 3-vote adversarial check produced a false negative.** Anything below
not marked verified above still carries that risk.

**Headline finding: the project's original framing was too strong and has been narrowed.**
"No good open-source Arabic solution exists" is false as stated. Real Arabic shaping and bidi
already exist in open embedded C. What does *not* exist is a Unicode-correct Arabic text pipeline
for **1-bit LED dot-matrix panels at 8–16px**. That is the gap this project fills, and the README
now says so.

---

## 1. On-MCU shaping is already solved — by LVGL

**LVGL (MIT) performs genuine runtime Arabic shaping and Unicode bidi on microcontrollers.**
This is the most important piece of prior art, and the project must not claim otherwise.

Two independent compile flags:

- **`LV_USE_ARABIC_PERSIAN_CHARS`** → real positional substitution in
  [`src/misc/lv_text_ap.c`](https://github.com/lvgl/lvgl/blob/master/src/misc/lv_text_ap.c).
  `ap_chars_map` holds isolated/initial/medial/final form offsets per character;
  `conj_to_previous`/`conj_to_next` encode joining rules; `lv_text_lam_alef()` emits the
  U+FEF5/U+FEFB lam-alef ligatures; `lv_text_is_arabic_vowel()` skips U+064B–U+0652 diacritics.
  Contributed in [PR #1409](https://github.com/lvgl/lvgl/pull/1409) by hamidrm, maintained
  through 9.4/9.6, and surfaced as its own Kconfig in Zephyr and Nordic's nRF Connect SDK.
- **`LV_USE_BIDI`** → LVGL's own `lv_bidi.c` (not a FriBidi binding), with
  `lv_obj_set_style_base_dir()` and `LV_BASE_DIR_LTR|RTL|AUTO`, affecting widget layout as well
  as text runs.

**Documented bounds** (these are why it doesn't close our gap, not criticisms):

- Display widgets only — text areas and `lv_label_set_text_static` are not processed
  (issues [#1274](https://github.com/lvgl/lvgl/issues/1274), #1888, #6700, #9404).
- Table-driven substitution, not HarfBuzz GSUB/GPOS — so it handles the common joining and
  lam-alef cases, not arbitrary OpenType features.
- `lv_text_ap_proc()` allocates per call.
- **Targets anti-aliased framebuffer/TFT GUIs**, not 1-bit 8–16px panels driven by
  MD_Parola / DMD32 / HUB75.

> **Open question worth answering before writing our own shaper:** can `lv_text_ap.c` and
> `lv_bidi.c` be extracted and reused standalone? They are MIT, so an AGPL project may use them.
> This could remove the need for a from-scratch shaper entirely.

Sources: [RTL docs](https://lvgl.io/docs/open/main-modules/fonts/rtl),
[font docs](https://docs.lvgl.io/9.2/overview/font.html), the source file and PR above.

## 2. Standalone Arduino reshapers — prior art for the technique

- [`HamidSaffari/UTF8_Persian_Arabic_Reshaper`](https://github.com/HamidSaffari/UTF8_Persian_Arabic_Reshaper)
  — the cleaner citation of the two.
- [`idreamsi/arduino-persian-reshaper`](https://github.com/idreamsi/arduino-persian-reshaper)
  — `Persian_Letters_Arduino.ino` computes joining state at runtime from neighbouring characters
  (`isFromTheSet1`/`isFromTheSet2` → `stat` = 0 isolated / 1 initial / 2 final / 3 medial) and
  indexes a different glyph per state, shipping an `8x8_FONT.bmp`.
- [`idreamsi/u8g2-persian-reshaper`](https://github.com/idreamsi/u8g2-persian-reshaper)

This is on-device positional shaping with a bitmap font — **exactly the technique**, and it
predates us. Qualifications: example sketches plus font data rather than a packaged library; a
joining-state machine over a fixed glyph table with no bidi, no general ligature handling and no
diacritic positioning; and it targets OLED/LCD.

## 3. The actual gap: LED-matrix libraries supply glyphs but refuse shaping

### u8g2 — drives MAX7219, ships Persian fonts, deliberately declines shaping

u8g2 genuinely drives dot-matrix hardware (`U8G2_MAX7219_8X8/16X16/32X8` constructors) and ships
a [Persian font group](https://github.com/olikraus/u8g2/wiki/fntgrppersian): Samim/Samim-FD and
GanjNamehSans-Regular at 10/12/14/16px, IranianSansRegular at 8/10/12/14/16px — **only
IranianSans has an 8px size that fits a single MAX7219 row.** The wiki page documents previews
and license text only: no contextual forms, no ligatures, no bidi.

The maintainer's position is explicit and durable. On a proposal to map U+0633 followed by
U+FEDD to U+FEB3, olikraus replied:

> "ok, but i think this should be done during typing in the string into Arduino IDE. I think this
> is not something for u8g2."
> — [#529](https://github.com/olikraus/u8g2/issues/529), 2018-03-07

Reaffirmed in [#640](https://github.com/olikraus/u8g2/issues/640) ("this will be too much for
u8g2 project") and [#1995](https://github.com/olikraus/u8g2/issues/1995) (2022, "You need to pass
a proper UTF-8 code sequence"). Users have filed disconnected-letter reports continuously —
#640, #1219, #2360, #2416, #2527, #2703 — **2018 through 2024, still unresolved.**

### Why u8g2 declined — verified directly, and it supports our architecture

The automated survey refuted this, wrongly. The primary source ([#529](https://github.com/olikraus/u8g2/issues/529),
fetched via the GitHub API on 2026-07-20) says it outright:

> "I tried already some time back. The kerning tables will be VERY huge. That is why I added
> `drawExtUTF8`. […] There is one more problem: From where shell I get the kerning information.
> I do not have the kerning information and I am not able to extract the same from .ttf files."

So the refusal was **resource-driven and tooling-driven**, not arbitrary: shaping/kerning tables
were too large for the target, and the maintainer had no path from `.ttf` to that data.

This is direct support for the host-shapes-then-ships-pixels split. The most experienced
maintainer in this space rejected on-MCU shaping for exactly the reasons the architecture
sidesteps — and note that issue #35 still needs to *measure* this rather than inherit it as
received wisdom.

Notably, u8g2 *did* gain `u8g2_DrawHB()` ([#2656](https://github.com/olikraus/u8g2/issues/2656),
June 2025) — but shaping runs in the **desktop `hb-shape` binary**, baked into a static PROGMEM
array by the `hbshape2u8g2` host tool. Offline toolchain shaping of fixed strings, not runtime
shaping. That *reinforces* the host-shapes-then-ships-pixels architecture rather than
contradicting it.

### ESPEasy P104 / MD_Parola — an Arabic font that can't render Arabic text

ESPEasy's [P104 plugin](https://espeasy.readthedocs.io/en/latest/Plugin/P104.html) (MD_Parola,
MAX7219) ships `P104_font_arabic.h` at `P104_ARABIC_FONT_ID 5`, inherited from MD_Parola
examples. Its own docs say the fonts are 8-bit codepage, not Unicode:

> "The system variables that produce special characters … actually generate Unicode characters,
> and that is not supported by the, ASCII based, fonts used"

> "The Arabic, Greek, Katakana and Cyrillic fonts have all their characters in the 'high-ascii'
> range (> 128 ascii values), so they can (and should) not be used to 'translate' normal text to
> Arabic"

P104's only direction affordance is a whole-string "Text reverse" content type, documented as
making "the Vertical font more usable" — **not bidi**.

To be precise: this font *is* a usable glyph set if fed correct high-ASCII indices. What it lacks
is Unicode mapping, shaping and bidi. Both u8g2 and P104 are legitimate prior art for the
"dumb glyph-index bitmap font, shaping done off-device" pattern and are credited as such.

> **Caveat:** MD_Parola itself was not directly verified — the one claim about its UTF-8 example
> was refuted 0–3. Statements here are inferred from the P104 derivative, which credits MD_Parola
> examples as its font source.

## 4. Commercial controllers — what can and cannot be said

**Read this section carefully; it is easy to overclaim and we already refuted several attempts.**

What survived verification is strictly *page-level and manual-level negatives*:

- **Huidu HD-W60/W62/W63/W64A** ([hdwell.com](https://www.hdwell.com/Product/index45.html), the
  vendor's own site): the page was fetched and tag-stripped to 4,214 characters of visible text;
  a regex scan for Arabic/RTL/right-to-left/language/Unicode/Persian/Urdu/Hebrew returned **zero
  hits**. Advertised features are borders, "a variety of text effects", "font hollow, stroke and
  other designs", "up to 20 content areas". The spec table lists loading capacity, max width,
  HUB12/HUB08, sensor, control mode — no language or charset field.
- **Onbon LedshowTW 2017**
  ([manual](https://en.onbonbx.com/upload/download/LedshowTW%202017%20software%20user%20manual.pdf)):
  states twice that the software "support simplified Chinese, traditional Chinese, English,
  Korean, Japanese, French, Russian, Thai, **Arabic**, German, Spanish, Portuguese, Vietnamese,
  the Kazakh, Croatian, Turkish, total of 16 kinds of languages" — i.e. Arabic appears as a
  **software-UI localization language**. Text-area properties expose only Font/size/Bold/Italic/
  underline/color.

**These are NOT proof that the hardware cannot display Arabic**, and laundering them into
"product X cannot render Arabic" would be a factual error. The same Huidu page names companion PC
software (HD2018/HD2020/HDsign/LED Art), and Onbon's software picks Windows fonts — so Windows
GDI/Uniscribe may well shape Arabic in practice without the manual documenting it.

Vendor pages are volatile point-in-time snapshots.

**The honest takeaway for positioning:** Arabic is not a *documented controller-firmware*
capability. Whatever shaping happens, happens upstream in closed Windows authoring software —
which means the commercial architecture is already host-shapes-then-ships-bitmap, the same split
this project uses. **That validates our architecture; it is not a differentiator.**

## 5. Reusable formats

[`lv_font_conv`](https://github.com/lvgl/lv_font_conv) (MIT) converts fonts "into a compact
bitmap format that fits small embedded systems", with `--format bin` and bpp 1/2/4 documented in
`doc/font_spec.md`. 1bpp — the only value we would need — is unambiguously supported.

Caveat: glyphs are bbox-cropped and bit-packed in a non-byte-aligned bitstream, so a consumer
needs a glyph-table/kern-table/bitstream decoder, not a `memcpy`. "Proprietary" here means
project-specific, not legally restricted.

**Licensing for reuse:** LVGL, `lv_font_conv` and u8g2 are permissively licensed, so their
formats and algorithms may be reused from this AGPL project. But u8g2's Persian fonts carry
**SIL OFL / Apache-2.0** terms that must be honoured separately.

---

## What this survey did NOT establish

Stated plainly, because these gaps affect our positioning:

1. **Our central novelty claim is unverified.** Nobody confirmed or refuted whether
   browser/canvas (or WASM HarfBuzz) shaping emitting a packed 1-bit framebuffer over
   HTTP/Web Serial/WebUSB/BLE to an ESP32 LED panel has been done before. **Do not assert this is
   novel** without a dedicated search. The commercial finding shows host-shapes-then-uploads is
   industry standard, so the browser instantiation may be the only new element — or may not be new
   at all.
2. **No academic work was surfaced** on Arabic legibility or glyph design at 8–16px. So we do not
   know whether we must design our own low-res Arabic glyph set, or which simplifications preserve
   readability.
3. **Nothing was verified about PxMatrix, DMD32, ESP32-HUB75-MatrixPanel-I2S-DMA,
   `arabic-reshaper`, or `python-bidi`** — all named in the original brief, none covered.
4. **The HarfBuzz-on-ESP32 cost tradeoff is unquantified** (flash/RAM/latency), so we cannot yet
   say whether browser-side shaping is a *necessity* or merely a convenience.

## Claims that were REFUTED — do not revive

Each failed a 3-vote adversarial check. Recorded so nobody re-adds them from memory.

| Refuted claim | Vote |
|---|---|
| Onbon BX-5E1 advertises built-in Arabic/Hebrew/Mongolian typesetting support | 0–3 |
| BX-5E1's Arabic shaping lives in host-side editor software rather than controller firmware | 0–3 |
| Huidu HD2018 exposes a per-text-area "Text direction [Right→Left]" checkbox | 0–3 |
| HD-W6X text rendering is driven entirely by vendor Windows software, with no on-device pipeline | 0–3 |
| LedshowTW's CHARSET selector implies Windows GDI rasterization | 0–3 |
| ~~Memory footprint was the stated blocker for shaping tables in u8g2~~ **RETRACTED — this was true; the refutation was wrong. See §3.** | 0–3 |
| MD_Parola's UTF-8 example proves its pipeline is fundamentally 8-bit-per-glyph | 0–3 |
| `arduino-persian-reshaper` targets only OLED/LCD, not LED matrices | 0–3 |
| The LedshowTW manual contains no mention of RTL/bidi anywhere | 1–2 |
| `arduino-persian-reshaper` is effectively unmaintained (~35 stars, no releases) | 1–2 |

## Where that leaves us

The defensible position, and the one the README now takes:

> An open, Unicode-correct (shaping **and** bidi) text pipeline targeting **1-bit LED dot-matrix
> panels at 8–16px** — a gap in the LED-matrix stack specifically. Not "nothing exists."

Two design consequences follow directly:

- **Evaluate reusing LVGL's `lv_text_ap.c` + `lv_bidi.c` before writing a shaper.** MIT, proven,
  already embedded-targeted.
- **Do not claim browser-side shaping is novel** until someone actually checks.
