/*
 * arabic-led-text-pipeline - ESP32 application entry point
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include <stdio.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "canvas.h"
#include "display.h"
#include "frame_rx.h"
#include "http_ui.h"
#include "text5x7.h"
#include "wifi_ap.h"

#ifdef CONFIG_DISPLAY_MAX7219
#  include "max7219.h"
#endif

static const char *TAG = "main";

/*
 * The application knows only the display interface. Which panel is attached is
 * a build-time choice resolved in display.c, so nothing here changes when a
 * new panel type is added.
 */
static const display_driver_t *s_disp;

static canvas_t s_screen;   /* exactly panel-sized: what gets rendered */

/*
 * Submitted content, which may be WIDER than the panel. The host sends the
 * whole rendered phrase once and the firmware windows across it, rather than
 * streaming a frame per animation step over the link.
 */
static canvas_t          s_content;
static SemaphoreHandle_t s_content_lock;
static volatile bool     s_scroll;
static volatile bool     s_rightward;
static volatile uint16_t s_speed_ms = 60;

/* Set once the first real frame arrives, so the self-test stops interfering. */
static volatile bool s_got_frame;

#if defined(CONFIG_DISPLAY_MAX7219) && CONFIG_MAX7219_ORIENTATION < 0
#  define MAPPING_UNKNOWN 1
#endif

/* ─── bring-up ────────────────────────────────────────────────────────────── */

#ifdef MAPPING_UNKNOWN
/*
 * Corner probe.
 *
 * Lights one corner at a time at a FIXED orientation (index 0, identity), so
 * every observation is interpretable. Watching where each logical corner
 * actually appears pins down transpose/flipX/flipY exactly - no guessing from
 * a glyph, and no cycling through candidates hoping one looks right.
 *
 * Report the four observed physical positions and the orientation index falls
 * straight out of them.
 */
static void bringup_pattern(void)
{
    const int w = s_disp->width();
    const int h = s_disp->height();

    const struct { int x, y; const char *name; } corners[] = {
        { 0,     0,     "TOP-LEFT"     },
        { w - 1, 0,     "TOP-RIGHT"    },
        { w - 1, h - 1, "BOTTOM-RIGHT" },
        { 0,     h - 1, "BOTTOM-LEFT"  },
    };

    /* Identity: any flip here would corrupt the very thing being measured. */
    max7219_set_mapping(max7219_active(), max7219_orientation(0));
    ESP_LOGW(TAG, "corner probe at orientation 0 (identity) - watch where each lands");

    while (!s_got_frame) {
        for (int i = 0; i < 4 && !s_got_frame; i++) {
            canvas_clear(&s_screen);
            canvas_set_mono(&s_screen, corners[i].x, corners[i].y, true);
            s_disp->render(&s_screen);
            ESP_LOGI(TAG, "corner %d/4: logical %s  (x=%d y=%d)",
                     i + 1, corners[i].name, corners[i].x, corners[i].y);
            vTaskDelay(pdMS_TO_TICKS(3000));
        }

        canvas_clear(&s_screen);
        s_disp->render(&s_screen);
        ESP_LOGI(TAG, "--- cycle end, blanking for 2s ---");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
#endif

#if defined(CONFIG_SELFTEST_ENABLE) && !defined(MAPPING_UNKNOWN)
/*
 * Scrolls a Latin message across the panel once. Proves the whole chain -
 * wiring, transport, geometry, packing - with text whose correct appearance
 * needs no interpretation. On an 8x8 panel only one character is visible at a
 * time, which is exactly why it scrolls.
 */
static void scroll_selftest(const char *msg)
{
    const int tw = text5x7_width(msg);
    const int y  = (s_screen.height - FONT5X7_HEIGHT) / 2;

    ESP_LOGI(TAG, "self-test: scrolling \"%s\" (%dpx) across %ux%u",
             msg, tw, s_screen.width, s_screen.height);

    for (int x = s_screen.width; x > -tw && !s_got_frame; x--) {
        canvas_clear(&s_screen);
        text5x7_draw(&s_screen, msg, x, y);
        s_disp->render(&s_screen);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_SELFTEST_SCROLL_MS));
    }
    canvas_clear(&s_screen);
    s_disp->render(&s_screen);
}
#endif

/* ─── frame handling ──────────────────────────────────────────────────────── */

/*
 * Copies a submitted frame into the content canvas. Content may be wider than
 * the panel; the display task is what maps it onto the LEDs.
 */
static void on_frame(const uint8_t *payload, const frame_meta_t *meta, void *user)
{
    (void)user;

    const canvas_fmt_t fmt  = (canvas_fmt_t)meta->fmt;
    const uint16_t     px_w = (fmt == CANVAS_RGB) ? meta->w_bytes
                                                  : (uint16_t)meta->w_bytes * 8;
    const size_t len = (fmt == CANVAS_RGB) ? (size_t)px_w * meta->h_rows * 3
                                           : (size_t)meta->w_bytes * meta->h_rows;

    if (meta->h_rows != s_screen.height) {
        ESP_LOGW(TAG, "frame is %u rows, panel is %u - dropping",
                 meta->h_rows, s_screen.height);
        return;
    }

    xSemaphoreTake(s_content_lock, portMAX_DELAY);

    if (s_content.width != px_w || s_content.fmt != fmt || s_content.data == NULL) {
        canvas_free(&s_content);
        if (!canvas_init(&s_content, px_w, meta->h_rows, fmt)) {
            ESP_LOGE(TAG, "content canvas alloc failed for %upx %s",
                     px_w, fmt == CANVAS_RGB ? "rgb" : "mono");
            xSemaphoreGive(s_content_lock);
            return;
        }
    }
    memcpy(s_content.data, payload, len);
    memcpy(s_content.colour, meta->colour, 3);

    s_scroll    = meta->scroll && (px_w > s_screen.width);
    s_rightward = meta->rightward;
    s_speed_ms  = meta->speed_ms;
    s_got_frame = true;

    xSemaphoreGive(s_content_lock);

    ESP_LOGI(TAG, "frame %ux%u %s, mode=%s%s%s",
             px_w, meta->h_rows, fmt == CANVAS_RGB ? "rgb" : "mono",
             s_scroll ? "scroll" : "static",
             s_scroll ? (s_rightward ? " rightward(RTL)" : " leftward(LTR)") : "",
             (meta->scroll && !s_scroll) ? " (fits panel, not scrolling)" : "");
}

/*
 * Blits a window of the content onto the screen canvas and pushes it.
 * `offset` is in pixels from the left of the content.
 *
 * Copies colour rather than just lit/unlit, so an RGB frame survives the trip
 * to a colour display. Reading out of bounds returns black, which is what lets
 * the window hang off either edge with no special case.
 */
static void blit_window(int offset)
{
    canvas_clear(&s_screen);
    for (int y = 0; y < s_screen.height; y++) {
        for (int x = 0; x < s_screen.width; x++) {
            uint8_t rgb[3];
            canvas_get_rgb(&s_content, offset + x, y, rgb);
            canvas_set_rgb(&s_screen, x, y, rgb);
        }
    }
    s_disp->render(&s_screen);
}

static void display_task(void *arg)
{
    (void)arg;
    int offset = 0;

    for (;;) {
        xSemaphoreTake(s_content_lock, portMAX_DELAY);

        if (s_content.data == NULL) {
            xSemaphoreGive(s_content_lock);
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }

        if (!s_scroll) {
            blit_window(0);
            xSemaphoreGive(s_content_lock);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        /*
         * Scroll with a panel-width gap, so the tail of the phrase does not
         * collide with its own head and read as one run-on string.
         *
         * Advancing the window rightward makes the content appear to travel
         * leftward, hence the inverted sign: Latin reads left-to-right so it
         * travels left, Arabic reads right-to-left so it travels right.
         */
        const int span = s_content.width + s_screen.width;
        blit_window(offset - s_screen.width);
        offset = s_rightward ? (offset - 1 + span) % span
                             : (offset + 1) % span;

        const uint16_t delay = s_speed_ms;
        xSemaphoreGive(s_content_lock);
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

/* ─── startup ─────────────────────────────────────────────────────────────── */

void app_main(void)
{
    s_disp = display_get();
    ESP_ERROR_CHECK(s_disp->init());

    const uint16_t w = s_disp->width(), h = s_disp->height();
    ESP_LOGI(TAG, "display: %s, %ux%u, colour=%s",
             s_disp->name, w, h, s_disp->has_colour() ? "yes" : "no");

    /*
     * The screen canvas matches the panel's own nature: mono for a MAX7219 or
     * P10, RGB for a strip. Content arriving in the other format is converted
     * on the way in, so either kind of frame renders on either kind of panel.
     */
    if (!canvas_init(&s_screen, w, h,
                     s_disp->has_colour() ? CANVAS_RGB : CANVAS_MONO)) {
        ESP_LOGE(TAG, "screen canvas allocation failed");
        return;
    }

    s_content_lock = xSemaphoreCreateMutex();
    xTaskCreate(display_task, "display", 4096, NULL, 4, NULL);

    ESP_ERROR_CHECK(wifi_ap_start(CONFIG_AP_SSID, CONFIG_AP_PASSWORD));
    ESP_ERROR_CHECK(http_ui_start(w, h, s_disp->has_colour(), on_frame, NULL));

#ifdef CONFIG_FRAME_UART_ENABLE
    ESP_ERROR_CHECK(frame_rx_start(CONFIG_FRAME_UART_NUM, CONFIG_FRAME_UART_BAUD,
                                   UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE,
                                   on_frame, NULL));
#endif

#ifdef MAPPING_UNKNOWN
    ESP_LOGW(TAG, "module mapping not configured - running corner probe.");
    ESP_LOGW(TAG, "Watch the panel, then set it via: idf.py menuconfig");
    bringup_pattern();
#elif defined(CONFIG_SELFTEST_ENABLE)
    scroll_selftest(CONFIG_SELFTEST_TEXT);
#endif

    ESP_LOGI(TAG, "ready - join \"%s\", then open http://192.168.4.1/",
             CONFIG_AP_SSID);
}
