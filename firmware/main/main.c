/*
 * arabic-led-text-pipeline - ESP32 application entry point
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "frame_rx.h"
#include "framebuffer.h"
#include "max7219.h"

static const char *TAG = "main";

static max7219_dev_t *s_panel;
static framebuffer_t  s_fb;

#if defined(CONFIG_MAX7219_MAP_ROW)
#  define CONFIGURED_MAPPING MAX7219_MAP_ROW_MAJOR
#elif defined(CONFIG_MAX7219_MAP_COL)
#  define CONFIGURED_MAPPING MAX7219_MAP_COL_MAJOR
#elif defined(CONFIG_MAX7219_MAP_ROW_REV)
#  define CONFIGURED_MAPPING MAX7219_MAP_ROW_MAJOR_REV
#elif defined(CONFIG_MAX7219_MAP_COL_REV)
#  define CONFIGURED_MAPPING MAX7219_MAP_COL_MAJOR_REV
#else
#  define CONFIGURED_MAPPING MAX7219_MAP_ROW_MAJOR   /* unknown: pattern mode */
#  define MAPPING_UNKNOWN 1
#endif

static void show(const char *what)
{
    ESP_LOGI(TAG, "pattern: %s", what);
    max7219_render(s_panel, &s_fb);
    vTaskDelay(pdMS_TO_TICKS(2500));
}

/*
 * Each step is asymmetric on purpose: a symmetric pattern looks identical
 * under several mappings and tells you nothing. Watch the panel, compare
 * against what the log says should be lit, and set the mapping in menuconfig.
 */
static void bringup_pattern(void)
{
    const uint16_t w = max7219_width(s_panel);
    const uint16_t h = max7219_height(s_panel);

    static const max7219_mapping_t candidates[] = {
        MAX7219_MAP_ROW_MAJOR,
        MAX7219_MAP_COL_MAJOR,
        MAX7219_MAP_ROW_MAJOR_REV,
        MAX7219_MAP_COL_MAJOR_REV,
    };
    static const char *names[] = { "ROW", "COL", "ROW_REV", "COL_REV" };

    for (;;) {
        for (int c = 0; c < 4; c++) {
            max7219_set_mapping(s_panel, candidates[c]);
            ESP_LOGI(TAG, "=== trying mapping %s ===", names[c]);

            fb_clear(&s_fb);
            fb_set_pixel(&s_fb, 0, 0, true);
            show("single pixel, should be TOP-LEFT corner");

            fb_clear(&s_fb);
            for (int x = 0; x < w; x++) {
                fb_set_pixel(&s_fb, x, 0, true);
            }
            show("horizontal line, should be the TOP edge, full width");

            fb_clear(&s_fb);
            for (int y = 0; y < h; y++) {
                fb_set_pixel(&s_fb, 0, y, true);
            }
            show("vertical line, should be the LEFT edge, 8px tall");

            /* An L: unambiguous under rotation and reflection. */
            fb_clear(&s_fb);
            for (int y = 0; y < h; y++) {
                fb_set_pixel(&s_fb, 0, y, true);
            }
            for (int x = 0; x < 5 && x < w; x++) {
                fb_set_pixel(&s_fb, x, h - 1, true);
            }
            show("letter L, upright, at the far LEFT");

            /* Lights modules left to right, revealing chain order. */
            for (int m = 0; m * 8 < w; m++) {
                fb_clear(&s_fb);
                for (int y = 0; y < h; y++) {
                    for (int x = m * 8; x < (m + 1) * 8 && x < w; x++) {
                        fb_set_pixel(&s_fb, x, y, true);
                    }
                }
                ESP_LOGI(TAG, "pattern: module %d of %d lit (counting from LEFT)",
                         m, w / 8);
                max7219_render(s_panel, &s_fb);
                vTaskDelay(pdMS_TO_TICKS(700));
            }
        }
    }
}

#ifndef MAPPING_UNKNOWN
static void on_frame(const uint8_t *payload, uint8_t w_bytes, uint8_t h_rows, void *user)
{
    (void)user;
    const size_t len = (size_t)w_bytes * h_rows;

    if (!fb_load_packed(&s_fb, payload, len)) {
        ESP_LOGW(TAG, "frame %ux%u (%u bytes) does not match panel buffer of %u",
                 w_bytes * 8, h_rows, (unsigned)len, (unsigned)s_fb.size);
        return;
    }
    max7219_render(s_panel, &s_fb);
    ESP_LOGI(TAG, "rendered frame %ux%u", w_bytes * 8, h_rows);
}
#endif

void app_main(void)
{
    const max7219_config_t cfg = {
        .pin_clk   = CONFIG_MAX7219_PIN_CLK,
        .pin_din   = CONFIG_MAX7219_PIN_DIN,
        .pin_cs    = CONFIG_MAX7219_PIN_CS,
        .modules   = CONFIG_MAX7219_MODULES,
        .intensity = CONFIG_MAX7219_INTENSITY,
        .mapping   = CONFIGURED_MAPPING,
    };

    ESP_ERROR_CHECK(max7219_init(&cfg, &s_panel));

    if (!fb_init(&s_fb, max7219_width(s_panel), max7219_height(s_panel))) {
        ESP_LOGE(TAG, "framebuffer allocation failed");
        return;
    }

#ifdef MAPPING_UNKNOWN
    ESP_LOGW(TAG, "module mapping not configured - running bring-up pattern.");
    ESP_LOGW(TAG, "Watch the panel, then set it via: idf.py menuconfig");
    bringup_pattern();   /* never returns */
#else
    ESP_ERROR_CHECK(frame_rx_start(CONFIG_FRAME_UART_NUM, CONFIG_FRAME_UART_BAUD,
                                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                   on_frame, NULL));
    ESP_LOGI(TAG, "waiting for frames");
#endif
}
