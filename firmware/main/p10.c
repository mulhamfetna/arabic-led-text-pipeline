/*
 * arabic-led-text-pipeline - P10 DMD panel (HUB12)
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "sdkconfig.h"
#ifdef CONFIG_DISPLAY_P10

#include "display.h"
#include "scan_matrix.h"

/*
 * A P10 panel is a pre-assembled scanned matrix: shift registers holding the
 * column data, a decoder and darlington sinks selecting and driving the rows.
 * HUB12 simply exposes those signals - R is the serial data, CLK the clock,
 * LAT the latch, A/B the row select, OE the blanking.
 *
 * So this is configuration over scan_matrix, not a driver of its own. The only
 * thing that distinguishes it from a hand-built 74HC595 matrix is that rows are
 * chosen by address lines rather than by a bit in the chain.
 *
 * 1/4 scan: the 16 rows are lit in four phases, phase p lighting rows p, p+4,
 * p+8 and p+12 together.
 *
 * ⚠ UNTESTED. Written against the HUB12 specification and never run on a
 * panel. The byte ordering these modules use varies by manufacturer, so treat
 * the geometry as unproven until it has been through the corner-probe bring-up
 * the MAX7219 went through.
 */

static esp_err_t drv_init(void)
{
    const scan_matrix_config_t cfg = {
        .name      = "P10",
        .pin_data  = CONFIG_P10_PIN_R,
        .pin_clk   = CONFIG_P10_PIN_CLK,
        .pin_latch = CONFIG_P10_PIN_LAT,
        .pin_oe    = CONFIG_P10_PIN_OE,
        .pin_addr  = { CONFIG_P10_PIN_A, CONFIG_P10_PIN_B, -1, -1 },
        .row_select     = ROW_SELECT_BINARY,
        .width          = CONFIG_P10_PANELS_X * 32,
        .height         = CONFIG_P10_PANELS_Y * 16,
        .rows_per_phase = 4,                    /* 1/4 scan */
        .brightness     = CONFIG_P10_BRIGHTNESS,
#ifdef CONFIG_P10_INVERT
        .invert         = true,
#else
        .invert         = false,
#endif
        .refresh_hz     = 100,
    };
    return scan_matrix_init(&cfg);
}

static uint16_t drv_width(void)      { return CONFIG_P10_PANELS_X * 32; }
static uint16_t drv_height(void)     { return CONFIG_P10_PANELS_Y * 16; }
static bool     drv_has_colour(void) { return false; }

const display_driver_t p10_display = {
    .name           = "P10 (untested)",
    .init           = drv_init,
    .deinit         = scan_matrix_deinit,
    .width          = drv_width,
    .height         = drv_height,
    .has_colour     = drv_has_colour,
    .render         = scan_matrix_render,
    .set_brightness = scan_matrix_set_brightness,
};

#endif /* CONFIG_DISPLAY_P10 */
