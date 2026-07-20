/*
 * arabic-led-text-pipeline - scanned LED matrix core
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "canvas.h"
#include "esp_err.h"

/*
 * Shared driver for panels that do not refresh themselves.
 *
 * A MAX7219 is written once and keeps scanning on its own. A P10 panel and a
 * hand-built 74HC595 matrix are just shift registers and row drivers: stop
 * feeding them and they go dark. So this core runs a timer that walks the scan
 * phases continuously, forever.
 *
 * A P10 is not a different device from a 595 matrix - it IS that circuit,
 * pre-assembled. Shift registers hold the column data, a decoder and darlington
 * sinks select and drive the rows, and HUB12 merely exposes the signals. The
 * only real difference is how a row is selected, which is why that is the one
 * thing parameterised here.
 */

typedef enum {
    /*
     * Address lines carry the row (P10's A/B pins, or a 74HC138 decoder).
     * Nothing about the row travels on the serial chain.
     */
    ROW_SELECT_BINARY,

    /*
     * The row is a one-hot bit inside the serial chain itself, in registers
     * ahead of the column registers. This is the hand-built arrangement.
     */
    ROW_SELECT_SHIFT_REG,
} row_select_t;

typedef struct {
    const char *name;

    /* Serial chain */
    int pin_data;
    int pin_clk;
    int pin_latch;
    int pin_oe;             /* active low; driven by LEDC for brightness */

    /* Row addressing, for ROW_SELECT_BINARY only. -1 for unused lines. */
    int pin_addr[4];

    row_select_t row_select;

    uint16_t width;         /* pixels */
    uint16_t height;        /* pixels */

    /*
     * Rows lit simultaneously per phase. P10 uses 4 (rows p, p+4, p+8, p+12);
     * a shift-register matrix lights one at a time.
     */
    uint8_t rows_per_phase;

    uint8_t  brightness;    /* 0-255 */
    bool     invert;        /* true where a 0 bit lights the LED */
    uint16_t refresh_hz;    /* whole-panel refresh; below ~50 it flickers */
} scan_matrix_config_t;

esp_err_t scan_matrix_init(const scan_matrix_config_t *cfg);
void      scan_matrix_deinit(void);
esp_err_t scan_matrix_render(const canvas_t *c);
esp_err_t scan_matrix_set_brightness(uint8_t level);
