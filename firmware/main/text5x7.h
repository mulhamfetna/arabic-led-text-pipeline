/*
 * arabic-led-text-pipeline - Latin text rendering for hardware self-test
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "font5x7.h"
#include "framebuffer.h"

/*
 * Reference-only Latin renderer.
 *
 * Its whole job is to answer "is the hardware chain correct?" with text whose
 * right appearance is obvious at a glance. Arabic never comes through here -
 * it is shaped by the browser and arrives already packed.
 */

#define TEXT5X7_ADVANCE 6   /* 5 glyph columns + 1 column of spacing */

/* Rendered width of s in pixels, excluding the trailing spacing column. */
int text5x7_width(const char *s);

/* Draws s with its top-left at (x, y). Clipping is handled by fb_set_pixel. */
void text5x7_draw(framebuffer_t *fb, const char *s, int x, int y);
