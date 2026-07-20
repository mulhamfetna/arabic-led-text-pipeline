/*
 * arabic-led-text-pipeline - HTTP UI and frame endpoint
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "canvas.h"
#include "esp_err.h"

/*
 * How a submitted frame should be presented.
 *
 * A scrolling frame is normally WIDER than the panel: the host sends the whole
 * rendered phrase once and the firmware windows across it, rather than
 * streaming one frame per animation step over the link.
 */
typedef struct {
    uint8_t  w_bytes;       /* frame width in bytes; pixel width is w_bytes*8 */
    uint8_t  h_rows;
    bool     scroll;
    /*
     * Which way the text appears to travel.
     *
     * Latin scrolls leftward: the string's left end is its first character, so
     * it must enter the panel first. Arabic reads right-to-left, so its first
     * character sits at the RIGHT end of the visual string - it has to travel
     * rightward to be revealed in reading order. Same mechanism, opposite sign.
     */
    bool     rightward;
    uint16_t speed_ms;      /* delay per 1px scroll step */

    /*
     * CANVAS_MONO or CANVAS_RGB. Mono keeps the payload at 1 bit per pixel and
     * carries `colour` to say what a lit pixel means on a display that has
     * colour; RGB carries the colour per pixel and ignores it.
     */
    uint8_t  fmt;
    uint8_t  colour[3];
} frame_meta_t;

typedef void (*http_frame_cb_t)(const uint8_t *payload, const frame_meta_t *meta,
                                void *user);

esp_err_t http_ui_start(uint16_t panel_w, uint16_t panel_h, bool has_colour,
                        http_frame_cb_t cb, void *user);
