/*
 * arabic-led-text-pipeline - display driver interface
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include <stdint.h>

#include "canvas.h"
#include "esp_err.h"

/*
 * Every panel type implements this.
 *
 * main.c, the HTTP endpoint, the serial receiver and the scroll task talk only
 * to these functions, so adding a panel means adding one file and editing
 * nothing else. The framebuffer was already panel-agnostic; this is what
 * finally makes the application layer agnostic too.
 *
 * A driver may know about geometry, colour and current draw. It must never
 * know anything about text - that is the whole architecture, and a driver that
 * starts reasoning about characters is the signal it has been violated.
 */
typedef struct {
    const char *name;

    esp_err_t (*init)(void);
    void      (*deinit)(void);

    uint16_t  (*width)(void);
    uint16_t  (*height)(void);

    /* True if the panel can show colour, so the UI knows whether to offer it. */
    bool      (*has_colour)(void);

    esp_err_t (*render)(const canvas_t *c);
    esp_err_t (*set_brightness)(uint8_t level);
} display_driver_t;

/* The driver selected at build time via menuconfig. Never NULL. */
const display_driver_t *display_get(void);
