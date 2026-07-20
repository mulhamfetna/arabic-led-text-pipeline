/*
 * arabic-led-text-pipeline - P10 DMD panel driver (HUB12)
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
#ifdef CONFIG_DISPLAY_P10
#include <stdlib.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include "display.h"


static const char *TAG = "p10";

/*
 * ⚠ UNTESTED. Written against the HUB12 specification and the published
 * behaviour of P10 panels; never run on real hardware.
 *
 * Do not trust the geometry until it has been through the same bring-up the
 * MAX7219 went through: light one corner at a time, observe where each lands,
 * and derive the mapping from the four observations rather than guessing. The
 * byte ordering below is the arrangement these panels commonly use, but P10
 * modules vary by manufacturer and this is exactly the kind of detail that is
 * wrong until proven right.
 *
 *
 * How a P10 differs from a MAX7219, which is the whole reason this driver is
 * more involved:
 *
 * A MAX7219 refreshes itself. Write the image once and the chip keeps scanning
 * for you. A P10 has no such logic - it is a bank of shift registers and row
 * drivers, and if the MCU stops feeding it, the panel goes dark. So this driver
 * runs a timer that continuously walks the scan phases, forever, in the
 * background.
 *
 * 1/4 scan: the 16 rows are lit in four phases. Phase p lights rows p, p+4,
 * p+8 and p+12 simultaneously, so a quarter of the panel is on at any instant
 * and persistence of vision does the rest. The A and B lines select the phase.
 *
 * Per phase the driver shifts out the four rows that will light, latches them
 * with LAT, sets A/B, then enables the drivers with OE. OE is driven by LEDC
 * rather than a plain GPIO so brightness is a duty cycle rather than an
 * all-or-nothing choice.
 */

#define PANEL_W      (CONFIG_P10_PANELS_X * 32)
#define PANEL_H      (CONFIG_P10_PANELS_Y * 16)
#define SCAN_PHASES  4
#define ROW_BYTES    (PANEL_W / 8)

/* Whole-panel refresh must clear ~50Hz or it visibly flickers; four phases at
   this period gives about 240Hz, which leaves margin for scheduling jitter. */
#define PHASE_PERIOD_US 1041

static spi_device_handle_t s_spi;
static uint8_t            *s_shadow;      /* PANEL_H rows x ROW_BYTES */
static uint8_t            *s_txbuf;       /* one phase: 4 rows          */
static esp_timer_handle_t  s_timer;
static volatile int        s_phase;
static portMUX_TYPE        s_mux = portMUX_INITIALIZER_UNLOCKED;

static inline void oe_enable(bool on)
{
    /* OE is active LOW on HUB12: duty 0 means fully on. */
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0,
                  on ? (1023 - CONFIG_P10_BRIGHTNESS * 1023 / 255) : 1023);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
}

/*
 * Emits one scan phase. Runs from a timer callback, so it must not block or
 * allocate - hence the preallocated transmit buffer and a polling SPI call.
 */
static void IRAM_ATTR scan_phase(void *arg)
{
    (void)arg;
    const int phase = s_phase;

    /*
     * The four rows lit together in this phase are 4 apart, which is what 1/4
     * scan means. They are shifted out last-row-first because the panel's shift
     * registers are daisy-chained the same way a MAX7219 cascade is: the first
     * byte sent travels furthest.
     */
    size_t n = 0;
    for (int r = SCAN_PHASES - 1; r >= 0; r--) {
        const int y = phase + r * SCAN_PHASES;
        if (y >= PANEL_H) {
            continue;
        }
        for (int b = 0; b < ROW_BYTES; b++) {
            uint8_t byte = s_shadow[(size_t)y * ROW_BYTES + b];
#ifdef CONFIG_P10_INVERT
            /* Many P10 panels drive their LEDs low-side, so a 0 bit lights. */
            byte = (uint8_t)~byte;
#endif
            s_txbuf[n++] = byte;
        }
    }

    oe_enable(false);                       /* blank while shifting */

    spi_transaction_t t = { .length = n * 8, .tx_buffer = s_txbuf };
    spi_device_polling_transmit(s_spi, &t);

    gpio_set_level(CONFIG_P10_PIN_LAT, 1);  /* latch the shifted row group */
    gpio_set_level(CONFIG_P10_PIN_LAT, 0);

    gpio_set_level(CONFIG_P10_PIN_A, phase & 1);
    gpio_set_level(CONFIG_P10_PIN_B, (phase >> 1) & 1);

    oe_enable(true);

    s_phase = (phase + 1) % SCAN_PHASES;
}

static esp_err_t drv_init(void)
{
    s_shadow = calloc((size_t)PANEL_H, ROW_BYTES);
    s_txbuf  = heap_caps_malloc((size_t)SCAN_PHASES * ROW_BYTES, MALLOC_CAP_DMA);
    if (!s_shadow || !s_txbuf) {
        return ESP_ERR_NO_MEM;
    }

    const gpio_config_t io = {
        .pin_bit_mask = (1ULL << CONFIG_P10_PIN_A) |
                        (1ULL << CONFIG_P10_PIN_B) |
                        (1ULL << CONFIG_P10_PIN_LAT),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&io));

    /* OE on LEDC so brightness is a duty cycle rather than on/off. */
    const ledc_timer_config_t lt = {
        .speed_mode      = LEDC_LOW_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_10_BIT,
        .timer_num       = LEDC_TIMER_0,
        .freq_hz         = 20000,
        .clk_cfg         = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&lt));
    const ledc_channel_config_t lc = {
        .gpio_num   = CONFIG_P10_PIN_OE,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 1023,             /* start blanked */
    };
    ESP_ERROR_CHECK(ledc_channel_config(&lc));

    const spi_bus_config_t bus = {
        .mosi_io_num     = CONFIG_P10_PIN_R,
        .miso_io_num     = -1,          /* the panel has no readback path */
        .sclk_io_num     = CONFIG_P10_PIN_CLK,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = SCAN_PHASES * ROW_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    const spi_device_interface_config_t dev = {
        .clock_speed_hz = 4 * 1000 * 1000,
        .mode           = 0,
        .spics_io_num   = -1,           /* LAT is driven by hand, not as CS */
        .queue_size     = 1,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(SPI2_HOST, &dev, &s_spi));

    const esp_timer_create_args_t targs = {
        .callback = scan_phase,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "p10_scan",
    };
    ESP_ERROR_CHECK(esp_timer_create(&targs, &s_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(s_timer, PHASE_PERIOD_US));

    ESP_LOGW(TAG, "P10 driver is UNTESTED - verify geometry before trusting it");
    ESP_LOGI(TAG, "ready: %dx%d px, 1/4 scan at ~%dHz",
             PANEL_W, PANEL_H, 1000000 / (PHASE_PERIOD_US * SCAN_PHASES));
    return ESP_OK;
}

static void drv_deinit(void)
{
    esp_timer_stop(s_timer);
    esp_timer_delete(s_timer);
    oe_enable(false);
    spi_bus_remove_device(s_spi);
    spi_bus_free(SPI2_HOST);
    free(s_shadow);
    heap_caps_free(s_txbuf);
}

static uint16_t drv_width(void)      { return PANEL_W; }
static uint16_t drv_height(void)     { return PANEL_H; }
static bool     drv_has_colour(void) { return false; }

static esp_err_t drv_brightness(uint8_t level)
{
    (void)level;    /* compile-time for now; see CONFIG_P10_BRIGHTNESS */
    return ESP_OK;
}

/*
 * Copies the canvas into the shadow buffer. The scan timer reads that buffer
 * continuously, so the copy is done under a critical section - a torn frame
 * would show as a band of the previous image across the panel.
 */
static esp_err_t drv_render(const canvas_t *c)
{
    portENTER_CRITICAL(&s_mux);
    for (int y = 0; y < PANEL_H; y++) {
        for (int b = 0; b < ROW_BYTES; b++) {
            uint8_t byte = 0;
            for (int bit = 0; bit < 8; bit++) {
                if (canvas_get_mono(c, b * 8 + bit, y, 128)) {
                    byte |= (uint8_t)(0x80u >> bit);
                }
            }
            s_shadow[(size_t)y * ROW_BYTES + b] = byte;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return ESP_OK;
}

const display_driver_t p10_display = {
    .name           = "P10 (untested)",
    .init           = drv_init,
    .deinit         = drv_deinit,
    .width          = drv_width,
    .height         = drv_height,
    .has_colour     = drv_has_colour,
    .render         = drv_render,
    .set_brightness = drv_brightness,
};

#endif /* CONFIG_DISPLAY_P10 */
