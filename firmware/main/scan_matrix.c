/*
 * arabic-led-text-pipeline - scanned LED matrix core
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "sdkconfig.h"

#if defined(CONFIG_DISPLAY_P10) || defined(CONFIG_DISPLAY_HC595)

#include "scan_matrix.h"

#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"

static const char *TAG = "scan_matrix";

static scan_matrix_config_t s_cfg;
static spi_device_handle_t  s_spi;
static uint8_t             *s_shadow;     /* height rows x stride bytes */
static uint8_t             *s_txbuf;      /* one phase, DMA-capable     */
static size_t               s_stride;     /* bytes per pixel row        */
static size_t               s_col_regs;
static size_t               s_row_regs;
static size_t               s_phases;
static esp_timer_handle_t   s_timer;
static volatile int         s_phase;
static portMUX_TYPE         s_mux = portMUX_INITIALIZER_UNLOCKED;

static void oe_set(bool enabled)
{
    /* OE is active LOW, so full duty means fully blanked. */
    const uint32_t duty = enabled
        ? (1023u - (uint32_t)s_cfg.brightness * 1023u / 255u)
        : 1023u;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/*
 * Builds one phase's worth of bytes and clocks it out.
 *
 * Runs from a timer callback, so it must not block or allocate - hence the
 * preallocated buffer and a polling SPI transfer.
 */
static void scan_phase(void *arg)
{
    (void)arg;
    const int phase = s_phase;
    size_t n = 0;

    /*
     * Column bytes are emitted FIRST because data shifts through the chain:
     * the byte sent first travels furthest, landing in the last column
     * register. Reversing this puts the row pattern into the column registers
     * and lights a smear rather than failing cleanly.
     *
     * Within a phase the rows are walked backwards for the same reason.
     */
    for (int r = s_cfg.rows_per_phase - 1; r >= 0; r--) {
        const int y = phase + r * (int)s_phases;
        if (y >= s_cfg.height) {
            continue;
        }
        for (size_t b = 0; b < s_stride; b++) {
            uint8_t byte = s_shadow[(size_t)y * s_stride + b];
            if (s_cfg.invert) {
                byte = (uint8_t)~byte;
            }
            s_txbuf[n++] = byte;
        }
    }

    /*
     * Then the row-select registers, one-hot across the whole chain. Row 0 is
     * the most significant bit of the first register; row 8 moves into the
     * second. See tools/verify_scan_matrix.py.
     */
    if (s_cfg.row_select == ROW_SELECT_SHIFT_REG) {
        for (size_t i = 0; i < s_row_regs; i++) {
            s_txbuf[n++] = 0;
        }
        const size_t base = n - s_row_regs;
        s_txbuf[base + (size_t)phase / 8] = (uint8_t)(0x80u >> (phase % 8));
    }

    oe_set(false);                          /* blank while shifting */

    spi_transaction_t t = { .length = n * 8, .tx_buffer = s_txbuf };
    spi_device_polling_transmit(s_spi, &t);

    gpio_set_level(s_cfg.pin_latch, 1);     /* latch the shifted phase */
    gpio_set_level(s_cfg.pin_latch, 0);

    if (s_cfg.row_select == ROW_SELECT_BINARY) {
        for (int i = 0; i < 4; i++) {
            if (s_cfg.pin_addr[i] >= 0) {
                gpio_set_level(s_cfg.pin_addr[i], (phase >> i) & 1);
            }
        }
    }

    oe_set(true);

    s_phase = (phase + 1) % (int)s_phases;
}

esp_err_t scan_matrix_init(const scan_matrix_config_t *cfg)
{
    s_cfg      = *cfg;
    s_stride   = ((size_t)cfg->width + 7) / 8;
    s_col_regs = s_stride;
    s_phases   = cfg->height / (cfg->rows_per_phase ? cfg->rows_per_phase : 1);
    s_row_regs = (cfg->row_select == ROW_SELECT_SHIFT_REG)
               ? (((size_t)cfg->height + 7) / 8) : 0;

    s_shadow = calloc((size_t)cfg->height, s_stride);
    s_txbuf  = heap_caps_malloc(s_stride * cfg->rows_per_phase + s_row_regs,
                                MALLOC_CAP_DMA);
    if (!s_shadow || !s_txbuf) {
        return ESP_ERR_NO_MEM;
    }

    uint64_t mask = 1ULL << cfg->pin_latch;
    for (int i = 0; i < 4; i++) {
        if (cfg->pin_addr[i] >= 0) {
            mask |= 1ULL << cfg->pin_addr[i];
        }
    }
    const gpio_config_t io = { .pin_bit_mask = mask, .mode = GPIO_MODE_OUTPUT };
    ESP_ERROR_CHECK(gpio_config(&io));

    /* OE on LEDC, so brightness is a duty cycle rather than on/off. */
    const ledc_timer_config_t lt = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 20000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&lt));
    const ledc_channel_config_t lc = {
        .gpio_num   = cfg->pin_oe,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 1023,                 /* start blanked */
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lc));

    const spi_bus_config_t bus = {
        .mosi_io_num     = cfg->pin_data,
        .miso_io_num     = -1,              /* no readback path exists */
        .sclk_io_num     = cfg->pin_clk,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = (int)(s_stride * cfg->rows_per_phase + s_row_regs),
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    const spi_device_interface_config_t dev = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = -1,               /* latch is driven by hand */
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    const uint64_t period_us = 1000000ULL / ((uint64_t)cfg->refresh_hz * s_phases);
    const esp_timer_create_args_t targs = {
        .callback = scan_phase,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "scan",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, period_us));

    ESP_LOGW(TAG, "%s is UNTESTED - verify geometry before trusting it", cfg->name);
    ESP_LOGI(TAG, "%s: %ux%u px, %u phases, 1/%u duty, %uHz refresh",
             cfg->name, cfg->width, cfg->height, (unsigned)s_phases,
             (unsigned)s_phases, cfg->refresh_hz);

    /*
     * Only one row group is lit at a time, so every extra row divides each
     * LED's on-time further. Worth saying out loud rather than leaving to be
     * discovered as "why is my tall panel dim".
     */
    if (s_phases > 16) {
        ESP_LOGW(TAG, "%u phases means each row is lit 1/%u of the time - "
                      "expect a dim panel", (unsigned)s_phases, (unsigned)s_phases);
    }
    return ESP_OK;
}

void scan_matrix_deinit(void)
{
    esp_timer_stop(s_timer);
    esp_timer_delete(s_timer);
    oe_set(false);
    spi_bus_remove_device(s_spi);
    spi_bus_free(SPI2_HOST);
    free(s_shadow);
    heap_caps_free(s_txbuf);
}

esp_err_t scan_matrix_set_brightness(uint8_t level)
{
    s_cfg.brightness = level;
    return ESP_OK;
}

/*
 * Copies the canvas into the shadow buffer.
 *
 * The scan timer reads that buffer continuously, so the copy happens inside a
 * critical section: a torn frame appears as a band of the previous image
 * across the panel.
 */
esp_err_t scan_matrix_render(const canvas_t *c)
{
    portENTER_CRITICAL(&s_mux);
    for (int y = 0; y < s_cfg.height; y++) {
        for (size_t b = 0; b < s_stride; b++) {
            uint8_t byte = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (canvas_get_mono(c, (int)(b * 8 + bit), y, 128)) {
                    byte |= (uint8_t)(0x80u >> bit);
                }
            }
            s_shadow[(size_t)y * s_stride + b] = byte;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

#endif /* CONFIG_DISPLAY_P10 || CONFIG_DISPLAY_HC595 */
