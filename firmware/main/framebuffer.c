/*
 * arabic-led-text-pipeline - 1-bit framebuffer
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "framebuffer.h"

#include <stdlib.h>
#include <string.h>

bool fb_init(framebuffer_t *fb, uint16_t width, uint16_t height)
{
    fb->width  = width;
    fb->height = height;
    fb->stride = (uint16_t)((width + 7) / 8);
    fb->size   = (size_t)fb->stride * height;
    fb->data   = calloc(1, fb->size);
    return fb->data != NULL;
}

void fb_free(framebuffer_t *fb)
{
    free(fb->data);
    fb->data = NULL;
    fb->size = 0;
}

void fb_clear(framebuffer_t *fb)
{
    memset(fb->data, 0, fb->size);
}

void fb_set_pixel(framebuffer_t *fb, int x, int y, bool on)
{
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return;
    }
    uint8_t *byte = &fb->data[(size_t)y * fb->stride + (size_t)(x / 8)];
    uint8_t  mask = (uint8_t)(0x80u >> (x % 8));
    if (on) {
        *byte |= mask;
    } else {
        *byte &= (uint8_t)~mask;
    }
}

bool fb_get_pixel(const framebuffer_t *fb, int x, int y)
{
    if (x < 0 || y < 0 || x >= fb->width || y >= fb->height) {
        return false;
    }
    uint8_t byte = fb->data[(size_t)y * fb->stride + (size_t)(x / 8)];
    return (byte & (uint8_t)(0x80u >> (x % 8))) != 0;
}

bool fb_load_packed(framebuffer_t *fb, const uint8_t *packed, size_t len)
{
    if (len != fb->size) {
        return false;
    }
    memcpy(fb->data, packed, len);
    return true;
}
