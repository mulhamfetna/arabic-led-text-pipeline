/*
 * arabic-led-text-pipeline - pixel container handed to display drivers
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    CANVAS_MONO = 0,    /* 1 bit per pixel, packed - the wire format verbatim */
    CANVAS_RGB  = 1,    /* 3 bytes per pixel, row-major                       */
} canvas_fmt_t;

/*
 * What a display driver is handed.
 *
 * Mono keeps the framebuffer's exact packing - bit 7 of byte 0 is the top-left
 * pixel - because that layout IS the wire format, and keeping them identical is
 * what makes the browser preview pixel-exact. Colour then says what a lit pixel
 * means on a display that has colour to give.
 *
 * A driver reads through canvas_get_rgb() or canvas_get_mono() and never needs
 * to know which format it was handed.
 */
typedef struct {
    uint16_t     width, height;
    canvas_fmt_t fmt;
    uint8_t     *data;
    size_t       size;
    uint8_t      colour[3];   /* mono only: colour of a lit pixel */
} canvas_t;

bool canvas_init(canvas_t *c, uint16_t w, uint16_t h, canvas_fmt_t fmt);
void canvas_free(canvas_t *c);
void canvas_clear(canvas_t *c);

/* Bytes per row for a mono canvas. Undefined for RGB. */
size_t canvas_mono_stride(const canvas_t *c);

/* Colour of (x,y) in any format. Out of bounds reads as black. */
void canvas_get_rgb(const canvas_t *c, int x, int y, uint8_t out[3]);

/*
 * Lit/unlit of (x,y) in any format. RGB is reduced by Rec. 601 luma against
 * `cutoff`. Out of bounds reads as unlit, which is what lets a scroll window
 * hang off either edge without special-casing.
 */
bool canvas_get_mono(const canvas_t *c, int x, int y, uint8_t cutoff);

/* Sets (x,y). Mono ignores colour beyond lit/unlit. */
void canvas_set_rgb(canvas_t *c, int x, int y, const uint8_t rgb[3]);
void canvas_set_mono(canvas_t *c, int x, int y, bool on);
