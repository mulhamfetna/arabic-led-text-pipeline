/*
 * arabic-led-text-pipeline - WS2812B addressable strip driver
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */

/*
 * Compiled only when this driver is selected. The guard sits above the
 * includes so an unselected driver costs nothing - not a header parse, not
 * a component dependency, and not a peripheral pulled in for hardware that
 * is not attached.
 */
#include "sdkconfig.h"
#ifdef CONFIG_DISPLAY_WS2812B
#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "led_strip.h"
#include "sdkconfig.h"

#include "display.h"


static const char *TAG = "ws2812b";

/*
 * WS2812B: one controller per LED, chained DIN->DOUT, 24 bits per LED, driven
 * by a single wire at 800kHz.
 *
 * The timing is far too tight to bit-bang reliably - a zero is a ~400ns pulse
 * and a one is ~800ns, with the difference measured in hundreds of nanoseconds
 * - so this drives the RMT peripheral through ESP-IDF's led_strip component.
 * RMT generates the waveform in hardware and is immune to whatever else the
 * CPU is doing.
 *
 * Note the part expects GRB order on the wire, not RGB. led_strip handles that
 * translation, which is why this file passes plain r,g,b and does not shuffle.
 */

#define LEDS ((size_t)CONFIG_WS2812B_COLS * CONFIG_WS2812B_ROWS)

static led_strip_handle_t s_strip;
static uint8_t           *s_rgb;      /* LEDS*3, in strip order */
static uint8_t            s_brightness = 255;

/*
 * Chain position for a pixel.
 *
 * Serpentine folds the strip at the end of each row, so alternate rows run
 * backwards - the usual hand-wired arrangement, since it needs no return wires.
 * Progressive keeps every row running the same way and needs one.
 *
 * Verified bijective in tools/verify_strip_layout.py: a collision here would
 * silently drop pixels rather than fail.
 */
static inline size_t strip_index(int x, int y)
{
#ifdef CONFIG_WS2812B_SERPENTINE
    if (y & 1) {
        x = CONFIG_WS2812B_COLS - 1 - x;
    }
#endif
    return (size_t)y * CONFIG_WS2812B_COLS + (size_t)x;
}

/*
 * Estimated supply current for the frame, in milliamps.
 *
 * A WS2812B draws roughly 20mA per fully-on channel, so one LED at white is
 * about 60mA and a 16x16 panel at white is nearly 15A. Nothing upstream is
 * obliged to know that, and a 24bpp path makes white a single colour-picker
 * click away - so the driver protects the hardware itself rather than trusting
 * the sender.
 *
 * The quiescent draw of the controllers (~1mA each) is deliberately excluded:
 * it is not something scaling the pixels can reduce, so including it would
 * make the clamp fight a load it cannot shed.
 */
static uint32_t estimate_ma(const uint8_t *rgb)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < LEDS * 3; i++) {
        sum += rgb[i];
    }
    return (uint32_t)((uint64_t)sum * 20u / 255u);
}

static esp_err_t drv_init(void)
{
    led_strip_config_t strip_cfg = {
        .strip_gpio_num   = CONFIG_WS2812B_PIN,
        .max_leds         = LEDS,
        .led_model        = LED_MODEL_WS2812,
        /* The part expects GRB on the wire; the component does that reorder,
           which is why drv_render passes plain r,g,b without shuffling. */
        .led_pixel_format = LED_PIXEL_FORMAT_GRB,
        .flags.invert_out = false,
    };
    led_strip_rmt_config_t rmt_cfg = {
        .clk_src       = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 10 * 1000 * 1000,   /* 10MHz: 0.1us per tick */
        .flags.with_dma = false,
    };

    esp_err_t err = led_strip_new_rmt_device(&strip_cfg, &rmt_cfg, &s_strip);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "led_strip init failed: %s", esp_err_to_name(err));
        return err;
    }

    s_rgb = calloc(LEDS, 3);
    if (!s_rgb) {
        return ESP_ERR_NO_MEM;
    }

    led_strip_clear(s_strip);

    ESP_LOGI(TAG, "ready: %dx%d = %u LEDs on GPIO%d, %s, budget %dmA",
             CONFIG_WS2812B_COLS, CONFIG_WS2812B_ROWS, (unsigned)LEDS,
             CONFIG_WS2812B_PIN,
#ifdef CONFIG_WS2812B_SERPENTINE
             "serpentine",
#else
             "progressive",
#endif
             CONFIG_WS2812B_MAX_MA);
    return ESP_OK;
}

static void drv_deinit(void)
{
    led_strip_clear(s_strip);
    led_strip_del(s_strip);
    free(s_rgb);
    s_rgb = NULL;
}

static uint16_t drv_width(void)      { return CONFIG_WS2812B_COLS; }
static uint16_t drv_height(void)     { return CONFIG_WS2812B_ROWS; }
static bool     drv_has_colour(void) { return true; }

static esp_err_t drv_brightness(uint8_t level)
{
    s_brightness = level;
    return ESP_OK;
}

static esp_err_t drv_render(const canvas_t *c)
{
    for (int y = 0; y < CONFIG_WS2812B_ROWS; y++) {
        for (int x = 0; x < CONFIG_WS2812B_COLS; x++) {
            uint8_t rgb[3];
            canvas_get_rgb(c, x, y, rgb);

            uint8_t *dst = &s_rgb[strip_index(x, y) * 3];
            /* Brightness first, so the current estimate sees what will actually
               be emitted rather than the requested value. */
            dst[0] = (uint8_t)((rgb[0] * s_brightness) / 255);
            dst[1] = (uint8_t)((rgb[1] * s_brightness) / 255);
            dst[2] = (uint8_t)((rgb[2] * s_brightness) / 255);
        }
    }

    const uint32_t want = estimate_ma(s_rgb);
    if (want > CONFIG_WS2812B_MAX_MA) {
        /*
         * Scale the whole frame rather than clipping bright pixels: clipping
         * would distort hue, whereas proportional scaling dims the image and
         * keeps the colours recognisable.
         */
        const uint32_t scale = ((uint32_t)CONFIG_WS2812B_MAX_MA << 8) / want;
        for (size_t i = 0; i < LEDS * 3; i++) {
            s_rgb[i] = (uint8_t)(((uint32_t)s_rgb[i] * scale) >> 8);
        }
        ESP_LOGW(TAG, "frame would draw ~%umA, scaled to fit %dmA budget",
                 (unsigned)want, CONFIG_WS2812B_MAX_MA);
    }

    for (size_t i = 0; i < LEDS; i++) {
        esp_err_t err = led_strip_set_pixel(s_strip, i,
                                            s_rgb[i * 3], s_rgb[i * 3 + 1],
                                            s_rgb[i * 3 + 2]);
        if (err != ESP_OK) {
            return err;
        }
    }
    return led_strip_refresh(s_strip);
}

const display_driver_t ws2812b_display = {
    .name           = "WS2812B",
    .init           = drv_init,
    .deinit         = drv_deinit,
    .width          = drv_width,
    .height         = drv_height,
    .has_colour     = drv_has_colour,
    .render         = drv_render,
    .set_brightness = drv_brightness,
};

#endif /* CONFIG_DISPLAY_WS2812B */
