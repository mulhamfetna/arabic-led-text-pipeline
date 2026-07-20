/*
 * arabic-led-text-pipeline - pixel container handed to display drivers
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "canvas.h"

#include <stdlib.h>
#include <string.h>

/*
 * Rec. 601 luma in 16-bit fixed point. The weights sum to exactly 65536, so the
 * divide is a shift.
 *
 * 8-bit weights (77/150/29 over 256) were tried first and drift from the
 * definition enough to flip 139 colours near the threshold - single pixels
 * appearing or vanishing with no visible cause. These track it to within one
 * least-significant bit; see tools/verify_canvas.py, which checks 140608
 * colours and permits disagreement only for colours sitting exactly on the
 * cutoff.
 *
 * Truncating rather than rounding is deliberate: floor(x) >= T is equivalent
 * to x >= T, so a plain shift reproduces the float predicate. Adding a
 * rounding term would instead test x >= T-0.5, a different question, and
 * flips hundreds of colours just below the cutoff.
 */
#define LUMA_R 19595u
#define LUMA_G 38470u
#define LUMA_B  7471u

bool canvas_init(canvas_t *c, uint16_t w, uint16_t h, canvas_fmt_t fmt)
{
    c->width  = w;
    c->height = h;
    c->fmt    = fmt;
    c->size   = (fmt == CANVAS_RGB) ? (size_t)w * h * 3
                                    : (size_t)((w + 7) / 8) * h;
    c->data   = calloc(1, c->size);
    c->colour[0] = c->colour[1] = c->colour[2] = 255;
    return c->data != NULL;
}

void canvas_free(canvas_t *c)
{
    free(c->data);
    c->data = NULL;
    c->size = 0;
}

void canvas_clear(canvas_t *c)
{
    memset(c->data, 0, c->size);
}

size_t canvas_mono_stride(const canvas_t *c)
{
    return ((size_t)c->width + 7) / 8;
}

static inline bool in_bounds(const canvas_t *c, int x, int y)
{
    return x >= 0 && y >= 0 && x < c->width && y < c->height;
}

void canvas_get_rgb(const canvas_t *c, int x, int y, uint8_t out[3])
{
    if (!in_bounds(c, x, y)) {
        out[0] = out[1] = out[2] = 0;
        return;
    }

    if (c->fmt == CANVAS_RGB) {
        const uint8_t *p = &c->data[((size_t)y * c->width + (size_t)x) * 3];
        out[0] = p[0]; out[1] = p[1]; out[2] = p[2];
        return;
    }

    const uint8_t byte = c->data[(size_t)y * canvas_mono_stride(c) + (size_t)(x / 8)];
    const bool on = (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
    out[0] = on ? c->colour[0] : 0;
    out[1] = on ? c->colour[1] : 0;
    out[2] = on ? c->colour[2] : 0;
}

bool canvas_get_mono(const canvas_t *c, int x, int y, uint8_t cutoff)
{
    if (!in_bounds(c, x, y)) {
        return false;
    }

    if (c->fmt == CANVAS_MONO) {
        const uint8_t byte = c->data[(size_t)y * canvas_mono_stride(c) + (size_t)(x / 8)];
        return (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
    }

    const uint8_t *p = &c->data[((size_t)y * c->width + (size_t)x) * 3];
    const uint32_t luma =
        (LUMA_R * p[0] + LUMA_G * p[1] + LUMA_B * p[2]) >> 16;
    return luma >= cutoff;
}

void canvas_set_rgb(canvas_t *c, int x, int y, const uint8_t rgb[3])
{
    if (!in_bounds(c, x, y)) {
        return;
    }

    if (c->fmt == CANVAS_RGB) {
        uint8_t *p = &c->data[((size_t)y * c->width + (size_t)x) * 3];
        p[0] = rgb[0]; p[1] = rgb[1]; p[2] = rgb[2];
        return;
    }

    /* Mono canvas: keep only whether there is any ink here. */
    const uint32_t luma = (LUMA_R * rgb[0] + LUMA_G * rgb[1] + LUMA_B * rgb[2]) >> 16;
    canvas_set_mono(c, x, y, luma >= 128);
}

void canvas_set_mono(canvas_t *c, int x, int y, bool on)
{
    if (!in_bounds(c, x, y)) {
        return;
    }

    if (c->fmt == CANVAS_MONO) {
        uint8_t *byte = &c->data[(size_t)y * canvas_mono_stride(c) + (size_t)(x / 8)];
        const uint8_t mask = (uint8_t)(0x80u >> (x % 8));
        if (on) {
            *byte |= mask;
        } else {
            *byte &= (uint8_t)~mask;
        }
        return;
    }

    uint8_t *p = &c->data[((size_t)y * c->width + (size_t)x) * 3];
    p[0] = on ? c->colour[0] : 0;
    p[1] = on ? c->colour[1] : 0;
    p[2] = on ? c->colour[2] : 0;
}
