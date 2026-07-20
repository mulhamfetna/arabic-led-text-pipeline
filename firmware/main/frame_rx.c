/*
 * arabic-led-text-pipeline - wire frame receiver
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "frame_rx.h"

#include <stdlib.h>
#include <string.h>

#include "driver/uart.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

static const char *TAG = "frame_rx";

#define RX_BUF_SIZE 2048

uint32_t frame_crc32(uint32_t seed, const uint8_t *data, size_t len)
{
    /*
     * Bitwise rather than table-driven: a 1KiB table is not worth the RAM for
     * frames this small, and it keeps the reference implementation obvious for
     * anyone porting the receiver to another MCU.
     */
    uint32_t crc = ~seed;
    for (size_t i = 0; i < len; i++) {
        crc ^= data[i];
        for (int b = 0; b < 8; b++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)(-(int32_t)(crc & 1)));
        }
    }
    return ~crc;
}

typedef struct {
    int        uart_num;
    frame_cb_t cb;
    void      *user;
    uint8_t   *payload;
} rx_ctx_t;

/* Blocking read of exactly `len` bytes; false on timeout. */
static bool read_exact(int uart_num, uint8_t *dst, size_t len, TickType_t timeout)
{
    size_t got = 0;
    while (got < len) {
        int n = uart_read_bytes(uart_num, dst + got, len - got, timeout);
        if (n <= 0) {
            return false;
        }
        got += (size_t)n;
    }
    return true;
}

static void rx_task(void *arg)
{
    rx_ctx_t *ctx = arg;
    const TickType_t byte_timeout = pdMS_TO_TICKS(500);

    for (;;) {
        uint8_t soh;
        /*
         * Resynchronisation is just "keep reading until SOH". A payload byte
         * that happens to equal 0x01 can therefore be mistaken for a start of
         * frame after a desync, and the CRC is what rejects it - see #6/#8 for
         * making this robust with a longer magic or byte stuffing.
         */
        if (!read_exact(ctx->uart_num, &soh, 1, portMAX_DELAY) || soh != FRAME_SOH) {
            continue;
        }

        uint8_t hdr[4];   /* w_bytes, h_rows, flags, speed */
        if (!read_exact(ctx->uart_num, hdr, sizeof(hdr), byte_timeout)) {
            ESP_LOGW(TAG, "header timeout");
            continue;
        }
        const uint8_t w_bytes = hdr[0];
        const uint8_t h_rows  = hdr[1];
        const uint8_t flags   = hdr[2];
        const uint8_t speed   = hdr[3];
        const size_t  len     = (size_t)w_bytes * h_rows;

        if (len == 0 || len > CONFIG_FRAME_MAX_PAYLOAD) {
            ESP_LOGW(TAG, "implausible geometry %ux%u (%u bytes), dropping",
                     w_bytes, h_rows, (unsigned)len);
            continue;
        }

        if (!read_exact(ctx->uart_num, ctx->payload, len, byte_timeout)) {
            ESP_LOGW(TAG, "payload timeout (wanted %u bytes)", (unsigned)len);
            continue;
        }

        uint8_t crc_le[4];
        if (!read_exact(ctx->uart_num, crc_le, sizeof(crc_le), byte_timeout)) {
            ESP_LOGW(TAG, "crc timeout");
            continue;
        }
        const uint32_t want = (uint32_t)crc_le[0]
                            | ((uint32_t)crc_le[1] << 8)
                            | ((uint32_t)crc_le[2] << 16)
                            | ((uint32_t)crc_le[3] << 24);

        uint32_t have = frame_crc32(0, hdr, sizeof(hdr));
        have = frame_crc32(have, ctx->payload, len);

        if (have != want) {
            /*
             * Drop and keep showing the previous frame. On a notice board a
             * briefly stale message beats a blanking or flickering panel.
             */
            ESP_LOGW(TAG, "crc mismatch: got %08x want %08x, keeping last frame",
                     (unsigned)have, (unsigned)want);
            continue;
        }

        const frame_meta_t meta = {
            .w_bytes  = w_bytes,
            .h_rows   = h_rows,
            .scroll    = (flags & FRAME_FLAG_SCROLL) != 0,
            .rightward = (flags & FRAME_FLAG_RIGHTWARD) != 0,
            .speed_ms = speed ? speed : 60,
        };
        ctx->cb(ctx->payload, &meta, ctx->user);
    }
}

esp_err_t frame_rx_start(int uart_num, int baud, int rx_pin, int tx_pin,
                         frame_cb_t cb, void *user)
{
    const uart_config_t uart_cfg = {
        .baud_rate = baud,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    esp_err_t err = uart_driver_install(uart_num, RX_BUF_SIZE, 0, 0, NULL, 0);
    if (err != ESP_OK) {
        return err;
    }
    if ((err = uart_param_config(uart_num, &uart_cfg)) != ESP_OK) {
        return err;
    }
    if ((err = uart_set_pin(uart_num, tx_pin, rx_pin,
                            UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE)) != ESP_OK) {
        return err;
    }

    rx_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }
    ctx->uart_num = uart_num;
    ctx->cb       = cb;
    ctx->user     = user;
    ctx->payload  = malloc(CONFIG_FRAME_MAX_PAYLOAD);
    if (!ctx->payload) {
        free(ctx);
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(rx_task, "frame_rx", 4096, ctx, 5, NULL) != pdPASS) {
        free(ctx->payload);
        free(ctx);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "listening on UART%d @ %d baud", uart_num, baud);
    return ESP_OK;
}
