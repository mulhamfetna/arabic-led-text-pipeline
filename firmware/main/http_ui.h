/*
 * arabic-led-text-pipeline - HTTP UI and frame endpoint
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdint.h>
#include <stddef.h>

#include "esp_err.h"

/* Invoked when the browser POSTs a packed frame. */
typedef void (*http_frame_cb_t)(const uint8_t *payload, uint8_t w_bytes,
                                uint8_t h_rows, void *user);

esp_err_t http_ui_start(uint16_t panel_w, uint16_t panel_h,
                        http_frame_cb_t cb, void *user);
