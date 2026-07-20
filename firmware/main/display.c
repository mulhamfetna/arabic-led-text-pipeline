/*
 * arabic-led-text-pipeline - display driver selection
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "display.h"
#include "sdkconfig.h"

/*
 * Resolved at build time rather than runtime. Only one panel is ever attached,
 * and linking all three drivers would drag in the RMT and LEDC peripherals for
 * hardware that is not present.
 */
#if defined(CONFIG_DISPLAY_WS2812B)
extern const display_driver_t ws2812b_display;
#elif defined(CONFIG_DISPLAY_P10)
extern const display_driver_t p10_display;
#elif defined(CONFIG_DISPLAY_HC595)
extern const display_driver_t hc595_display;
#else
extern const display_driver_t max7219_display;
#endif

const display_driver_t *display_get(void)
{
#if defined(CONFIG_DISPLAY_WS2812B)
    return &ws2812b_display;
#elif defined(CONFIG_DISPLAY_P10)
    return &p10_display;
#elif defined(CONFIG_DISPLAY_HC595)
    return &hc595_display;
#else
    return &max7219_display;
#endif
}
