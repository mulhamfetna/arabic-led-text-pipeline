/*
 * arabic-led-text-pipeline - 74HC595 shift-register matrix
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "sdkconfig.h"
#ifdef CONFIG_DISPLAY_HC595

#include "display.h"
#include "scan_matrix.h"

/*
 * A hand-built matrix: 74HC595 shift registers for the column data, more of
 * them carrying a one-hot row-select bit, and a transistor array sinking the
 * actual row current.
 *
 * This is the same circuit a P10 panel contains, which is why both are
 * configurations of scan_matrix rather than separate drivers. The only
 * difference is that a P10 selects rows with address lines, and this selects
 * them with bits in the chain.
 *
 * ⚠ The row-select outputs MUST drive a ULN2803 or equivalent, never the LEDs
 * directly. A 74HC595 pin is rated 35mA and the package 70mA; a row of eight
 * lit LEDs at 20mA each is 160mA. Wiring rows straight to the 595 damages it.
 *
 * Columns need one current-limiting resistor each - a MAX7219 provides this
 * internally, a 595 does not.
 */

static esp_err_t drv_init(void)
{
    const scan_matrix_config_t cfg = {
        .name      = "74HC595",
        .pin_data  = CONFIG_HC595_PIN_DATA,
        .pin_clk   = CONFIG_HC595_PIN_CLK,
        .pin_latch = CONFIG_HC595_PIN_LATCH,
        .pin_oe    = CONFIG_HC595_PIN_OE,
        .pin_addr  = { -1, -1, -1, -1 },       /* rows ride the chain */
        .row_select     = ROW_SELECT_SHIFT_REG,
        .width          = CONFIG_HC595_COL_REGS * 8,
        .height         = CONFIG_HC595_ROW_REGS * 8,
        .rows_per_phase = 1,                    /* one row lit at a time */
        .brightness     = CONFIG_HC595_BRIGHTNESS,
        .invert         = false,
        .refresh_hz     = CONFIG_HC595_REFRESH_HZ,
    };
    return scan_matrix_init(&cfg);
}

static uint16_t drv_width(void)      { return CONFIG_HC595_COL_REGS * 8; }
static uint16_t drv_height(void)     { return CONFIG_HC595_ROW_REGS * 8; }
static bool     drv_has_colour(void) { return false; }

const display_driver_t hc595_display = {
    .name           = "74HC595 matrix (untested)",
    .init           = drv_init,
    .deinit         = scan_matrix_deinit,
    .width          = drv_width,
    .height         = drv_height,
    .has_colour     = drv_has_colour,
    .render         = scan_matrix_render,
    .set_brightness = scan_matrix_set_brightness,
};

#endif /* CONFIG_DISPLAY_HC595 */
