# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Status

**Design-stage only — there is no code, build system, or test suite yet.** The source of truth for
the design is `about.md` (pipeline, protocol sketch, product concept) and `crrunt-hradware.md`
(hardware actually on hand; note the typo'd filename — keep it or rename deliberately, don't create
a duplicate).

Published as `molhamfetnah/arabic-led-text-pipeline` (AGPL-3.0-or-later, Zenodo-archived). The local
directory is still named `arabic-p10dmd`; the repo name is deliberately broader because the project
expands past P10. It sits inside the `/mnt/data/projects` multi-project workspace — see the parent
`CLAUDE.md` for workspace conventions.

## Workflow — non-negotiable

`main` and `dev` are both protected; **never commit directly to either**. Every change, however
small, goes: branch off `dev` → PR into `dev` → squash merge. `dev` → `main` only for releases,
via a merge commit, then a tag (which triggers Zenodo to mint a DOI).

Branch prefixes: `feat/`, `fix/`, `docs/`, `chore/`. Work is tracked as `epic`-labelled umbrella
issues containing task lists of child issues; PRs close issues with `Closes #N`. Full detail in
`CONTRIBUTING.md` — read it before starting work.

Bump `version` and `date-released` in `CITATION.cff` before any release tag.

## Output rule

Every generated artifact is a **local file**. Do not publish Artifacts, cloud pages, or upload to
any external service. Pushing to the `origin` remote and using GitHub issues/PRs is expected and
fine — that is this project's normal version control and planning, not publishing.

## The problem this project solves

Off-the-shelf P10 LED-panel controllers use bitmap fonts and cannot render Arabic correctly. Arabic
is cursive and RTL: each letter has initial/medial/final/isolated forms, and ligatures (لا, الله)
must be substituted. The whole architecture exists to do **real Unicode shaping on the PC side** and
ship the panel nothing but a pre-rendered 1-bit framebuffer, so the MCU stays dumb.

## Architecture: where the work is split

The split between host and MCU is the single most important design decision — preserve it.

**Host (Windows/Android app)** does everything hard, in this order:
1. Arabic shaping — HarfBuzz or `arabic_reshaper`, produces the visual-order glyph string
2. Render — PIL/Qt draws the shaped text with a TTF (Amiri/Lateef), 4× supersampled
3. Resize/letterbox to exact panel dimensions (no distortion)
4. Threshold to 1-bit (128 cutoff, tunable ~100–180 to trade stroke thickness)
5. Pack 8 horizontal pixels per byte, row-major
6. Frame it with a header + CRC and transmit over UART/WiFi

**MCU (ESP32)** only receives, parses the frame, loads a framebuffer, and drives the panel scan.
It contains no font, no shaping, no text logic. If you find yourself wanting to put text handling
on the MCU, that's a signal the design is being violated.

### Wire format

`SOH(0x01) | W_bytes | H_rows | ... | [data] | CRC32`

For 64×32: 8 bytes/row × 32 rows = 256 bytes of payload. The byte array maps 1:1 onto physical LEDs
with `array[0]` bit 7 = top-left pixel. Preserving that exact correspondence is what makes the host
preview pixel-accurate.

## Hardware: two targets, don't conflate them

`about.md` describes the **eventual** target: 2× P10-1R-1S (32×16 red) chained to 64×32, driven via
DMD32/PxMatrix, ESP32 in WiFi AP mode at `192.168.4.1:8080`.

`crrunt-hradware.md` describes what is being **built against now**: ESP32 + a MAX7219 dot-matrix
module, used as a cheap bring-up target. The stated intent is to expand later to P10, and further to
multicolor/smart-LED chains.

Practical consequence: the pipeline through bit-packing should stay **panel-agnostic**. Only the
final driver layer and the width/height in the frame header should know whether the target is
MAX7219, P10, or an addressable chain. (Note MAX7219 is column-addressed 8×8 tiles, not the
row-major horizontal packing P10 wants — expect that translation to live in the driver/firmware,
not in the host pipeline.)

## Conventions to hold to

- The host preview must be a 1:1 pixel match to the panel — it is the project's main advantage over
  closed-source alternatives, so any rendering change must be applied to preview and output together.
- Open protocol is a goal: any MCU should be able to implement the receiver from the frame spec alone.
