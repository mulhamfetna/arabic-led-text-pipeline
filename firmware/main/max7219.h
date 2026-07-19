/*
 * arabic-led-text-pipeline - MAX7219 cascade driver
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "framebuffer.h"

/*
 * How a module's 8 "digit" registers map onto its 8x8 LED grid.
 *
 * This is a property of the physical board, not of the MAX7219 chip: generic
 * modules wire the digit and segment lines to the grid differently, and there
 * is no way to read it back over SPI. Determine it once with the bring-up
 * pattern, then set it in menuconfig.
 */
typedef enum {
    MAX7219_MAP_ROW_MAJOR,      /* digit N = pixel row N, bit 7 = leftmost   */
    MAX7219_MAP_COL_MAJOR,      /* digit N = pixel column N, bit 7 = topmost */
    MAX7219_MAP_ROW_MAJOR_REV,  /* row-addressed, bit 0 = leftmost           */
    MAX7219_MAP_COL_MAJOR_REV,  /* column-addressed, bit 0 = topmost         */
} max7219_mapping_t;

typedef struct {
    int  pin_clk;
    int  pin_din;
    int  pin_cs;
    int  modules;               /* cascaded 8x8 units */
    uint8_t intensity;          /* 0-15 */
    max7219_mapping_t mapping;
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

/*
 * Pushes the framebuffer to the cascade, translating from the wire format's
 * row-major packing into whatever the modules expect. fb may be larger than
 * the panel; the top-left region is shown.
 */
esp_err_t max7219_render(max7219_dev_t *dev, const framebuffer_t *fb);
