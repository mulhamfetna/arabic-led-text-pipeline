/*
 * arabic-led-text-pipeline - Latin text rendering for hardware self-test
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "text5x7.h"

#include <string.h>

#include "font5x7.h"

int text5x7_width(const char *s)
{
    const int n = (int)strlen(s);
    return n > 0 ? n * TEXT5X7_ADVANCE - 1 : 0;
}

void text5x7_draw(canvas_t *c, const char *s, int x, int y)
{
    for (const char *p = s; *p; p++, x += TEXT5X7_ADVANCE) {
        /* Skip glyphs that fall entirely outside the panel. */
        if (x + FONT5X7_WIDTH < 0) {
            continue;
        }
        if (x >= c->width) {
            break;
        }

        const uint8_t *glyph = font5x7_glyph(*p);
        for (int col = 0; col < FONT5X7_WIDTH; col++) {
            const uint8_t bits = glyph[col];
            for (int row = 0; row < FONT5X7_HEIGHT; row++) {
                if (bits & (1u << row)) {
                    canvas_set_mono(c, x + col, y + row, true);
                }
            }
        }
    }
}
