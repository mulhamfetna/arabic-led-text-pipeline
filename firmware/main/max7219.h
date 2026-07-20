/*
 * arabic-led-text-pipeline - MAX7219 cascade driver
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "canvas.h"
#include "display.h"

/*
 * How a module's 8 "digit" registers map onto its 8x8 LED grid.
 *
 * This is a property of the physical board, not of the MAX7219 chip: generic
 * modules wire the digit and segment lines to the grid differently, and there
 * is no way to read it back over SPI (the part has no MISO). Determine it once
 * with the bring-up pattern, then set it in menuconfig.
 *
 * The space is exactly the eight symmetries of a square: whether the digit
 * axis runs along rows or columns (transpose), times a flip on each axis.
 * An earlier version offered only four of these - flipping the bit order but
 * never the digit order - and could not describe a module whose digit 0 is the
 * bottom row. All eight are needed.
 */
typedef struct {
    bool transpose;     /* digit selects a column rather than a row */
    bool flip_x;
    bool flip_y;
} max7219_mapping_t;

#define MAX7219_ORIENTATION_COUNT 8

/* Orientation for index 0..7, for cycling through candidates at bring-up. */
max7219_mapping_t max7219_orientation(int index);

/* Human-readable name, e.g. "transpose+flipY". Valid for index 0..7. */
const char *max7219_orientation_name(int index);

/*
 * How chain position maps onto a 2D arrangement of modules.
 *
 * The modules are always one electrical daisy-chain, but physically they may
 * be laid out as a grid. Which module the Nth link lands on depends on how the
 * builder ran the ribbon between rows.
 */
typedef enum {
    MAX7219_CHAIN_ROW_MAJOR,    /* every row restarts at the left  */
    MAX7219_CHAIN_SERPENTINE,   /* alternate rows run right-to-left */
} max7219_chain_t;

typedef struct {
    int  pin_clk;
    int  pin_din;
    int  pin_cs;
    int  cols;                  /* modules across: panel is cols*8 px wide */
    int  rows;                  /* modules down:  panel is rows*8 px tall  */
    uint8_t intensity;          /* 0-15 */
    max7219_mapping_t mapping;
    max7219_chain_t   chain;
} max7219_config_t;

typedef struct max7219_dev max7219_dev_t;

esp_err_t max7219_init(const max7219_config_t *cfg, max7219_dev_t **out);
void      max7219_deinit(max7219_dev_t *dev);

/* Panel geometry implied by the cascade: modules*8 wide, 8 tall. */
uint16_t max7219_width(const max7219_dev_t *dev);
uint16_t max7219_height(const max7219_dev_t *dev);

esp_err_t max7219_set_intensity(max7219_dev_t *dev, uint8_t intensity);
esp_err_t max7219_set_mapping(max7219_dev_t *dev, max7219_mapping_t mapping);
esp_err_t max7219_clear(max7219_dev_t *dev);

/* Renders a canvas. An RGB canvas is reduced by luma, since this panel is
   monochrome and cannot do better than lit/unlit. */
esp_err_t max7219_render_canvas(max7219_dev_t *dev, const canvas_t *c);

/* The display_driver_t face of this panel; see display.h. */
extern const display_driver_t max7219_display;

/* The live device, for the bring-up probe that varies orientation at runtime. */
max7219_dev_t *max7219_active(void);
