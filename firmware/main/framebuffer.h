/*
 * arabic-led-text-pipeline - 1-bit framebuffer
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
 * Packed 1-bit, row-major, MSB first: bit 7 of byte 0 is the top-left pixel.
 * This layout is the wire format, stored verbatim - the host and the panel
 * agree on it exactly, which is what makes the host preview pixel-accurate.
 * Do not "optimise" this into a different in-memory layout.
 */
typedef struct {
    uint16_t width;         /* pixels */
    uint16_t height;        /* pixels */
    uint16_t stride;        /* bytes per row = ceil(width / 8) */
    uint8_t *data;
    size_t   size;          /* stride * height */
} framebuffer_t;

/* Allocates data; returns false on OOM. */
bool fb_init(framebuffer_t *fb, uint16_t width, uint16_t height);
void fb_free(framebuffer_t *fb);

void fb_clear(framebuffer_t *fb);

/* Out-of-bounds coordinates are ignored (set) or read as 0 (get). */
void fb_set_pixel(framebuffer_t *fb, int x, int y, bool on);
bool fb_get_pixel(const framebuffer_t *fb, int x, int y);

/*
 * Copies a packed payload straight in. Returns false if len does not match
 * fb->size, which would mean the header and the payload disagree.
 */
bool fb_load_packed(framebuffer_t *fb, const uint8_t *packed, size_t len);
