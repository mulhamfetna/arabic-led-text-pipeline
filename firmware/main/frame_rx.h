/*
 * arabic-led-text-pipeline - wire frame receiver
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

/*
 * Wire format (see docs/wire-format.md):
 *
 *   0x01 | w_bytes | h_rows | payload[w_bytes * h_rows] | crc32[4]
 *
 * crc32 is CRC-32/ISO-HDLC (poly 0xEDB88320 reflected, init 0xFFFFFFFF,
 * final XOR 0xFFFFFFFF - the same value Python's zlib.crc32 returns),
 * transmitted little-endian, computed over w_bytes, h_rows and the payload.
 *
 * NOTE: this is the provisional format. It is not frozen until #6 closes.
 */
#define FRAME_SOH 0x01

/* Computed over `len` bytes; seed with 0 for a fresh CRC. */
uint32_t frame_crc32(uint32_t seed, const uint8_t *data, size_t len);

/*
 * Called from the receive task when a frame arrives with a valid CRC.
 * w_bytes*8 is the pixel width; h_rows is the pixel height.
 */
typedef void (*frame_cb_t)(const uint8_t *payload, uint8_t w_bytes, uint8_t h_rows,
                           void *user);

/* Starts the UART receive task. Frames with bad CRCs are dropped (see #8). */
esp_err_t frame_rx_start(int uart_num, int baud, int rx_pin, int tx_pin,
                         frame_cb_t cb, void *user);
