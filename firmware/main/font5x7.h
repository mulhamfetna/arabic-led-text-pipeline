/*
 * arabic-led-text-pipeline - 5x7 ASCII font
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

/*
 * Latin-only bitmap font, deliberately.
 *
 * This exists to prove the hardware chain - wiring, SPI, cascade order, pixel
 * mapping, packing - using text whose correct appearance nobody has to think
 * about. It is NOT the project's text path: Arabic is shaped by the browser
 * and arrives as pixels, and adding Arabic glyphs here would be exactly the
 * bitmap-font mistake this project exists to avoid.
 *
 * Column-major: each glyph is 5 columns, one byte per column, bit 0 = top row.
 */
#define FONT5X7_FIRST   0x20    /* space */
#define FONT5X7_LAST    0x7E    /* ~ */
#define FONT5X7_WIDTH   5
#define FONT5X7_HEIGHT  7

extern const uint8_t font5x7[(FONT5X7_LAST - FONT5X7_FIRST + 1)][FONT5X7_WIDTH];

/* Returns the glyph for c, or the glyph for '?' if it is outside the range. */
const uint8_t *font5x7_glyph(char c);
