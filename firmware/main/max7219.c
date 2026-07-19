/*
 * arabic-led-text-pipeline - MAX7219 cascade driver
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "max7219.h"

#include <stdlib.h>
#include <string.h>

#include "driver/spi_master.h"
#include "esp_log.h"

static const char *TAG = "max7219";

/* MAX7219 register addresses (datasheet table 2). */
#define REG_NOOP        0x00
#define REG_DIGIT0      0x01    /* digits 0..7 are 0x01..0x08 */
#define REG_DECODEMODE  0x09
#define REG_INTENSITY   0x0A
#define REG_SCANLIMIT   0x0B
#define REG_SHUTDOWN    0x0C
#define REG_DISPLAYTEST 0x0F

/*
 * Breadboard-friendly. The part is rated to 10MHz, but MAX7219 modules are
 * usually reached over long dupont jumpers where that does not survive.
 */
#define SPI_CLOCK_HZ    (1 * 1000 * 1000)

#define MODULE_PX       8       /* each module is 8x8 */

struct max7219_dev {
    spi_device_handle_t spi;
    spi_host_device_t   host;
    int      modules;
    max7219_mapping_t mapping;
    uint8_t *txbuf;             /* modules * 2 bytes, DMA-capable */
};

/*
 * Writes one register across the whole cascade in a single CS-low transaction.
 *
 * Data shifts through the chain, so the word transmitted first ends up in the
 * module furthest from DIN. `data` is indexed by chain position (0 = nearest
 * DIN), and this walks it backwards to compensate.
 */
static esp_err_t write_register(max7219_dev_t *dev, uint8_t reg, const uint8_t *data)
{
    for (int i = 0; i < dev->modules; i++) {
        int tx = dev->modules - 1 - i;
        dev->txbuf[tx * 2 + 0] = reg;
        dev->txbuf[tx * 2 + 1] = data[i];
    }

    spi_transaction_t t = {
        .length    = (size_t)dev->modules * 16,
        .tx_buffer = dev->txbuf,
    };
    return spi_device_transmit(dev->spi, &t);
}

/* Same register and value to every module in the chain. */
static esp_err_t write_register_all(max7219_dev_t *dev, uint8_t reg, uint8_t value)
{
    uint8_t *same = malloc((size_t)dev->modules);
    if (!same) {
        return ESP_ERR_NO_MEM;
    }
    memset(same, value, (size_t)dev->modules);
    esp_err_t err = write_register(dev, reg, same);
    free(same);
    return err;
}

esp_err_t max7219_init(const max7219_config_t *cfg, max7219_dev_t **out)
{
    if (!cfg || !out || cfg->modules < 1) {
        return ESP_ERR_INVALID_ARG;
    }

    max7219_dev_t *dev = calloc(1, sizeof(*dev));
    if (!dev) {
        return ESP_ERR_NO_MEM;
    }
    dev->modules = cfg->modules;
    dev->mapping = cfg->mapping;
    dev->host    = SPI3_HOST;   /* VSPI */

    dev->txbuf = heap_caps_malloc((size_t)cfg->modules * 2, MALLOC_CAP_DMA);
    if (!dev->txbuf) {
        free(dev);
        return ESP_ERR_NO_MEM;
    }

    spi_bus_config_t bus = {
        .mosi_io_num     = cfg->pin_din,
        .miso_io_num     = -1,      /* MAX7219 has no readback path */
        .sclk_io_num     = cfg->pin_clk,
        .quadwp_io_num   = -1,
        .quadhd_io_num   = -1,
        .max_transfer_sz = cfg->modules * 2,
    };
    esp_err_t err = spi_bus_initialize(dev->host, &bus, SPI_DMA_CH_AUTO);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(err));
        goto fail_buf;
    }

    /*
     * MAX7219 latches the shift register on the rising edge of CS (LOAD), and
     * the ESP32 driver raises CS at end-of-transaction, so driver-managed CS
     * gives exactly the required behaviour.
     */
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = SPI_CLOCK_HZ,
        .mode           = 0,        /* CPOL=0, CPHA=0 */
        .spics_io_num   = cfg->pin_cs,
        .queue_size     = 1,
    };
    err = spi_bus_add_device(dev->host, &devcfg, &dev->spi);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(err));
        goto fail_bus;
    }

    /* Order matters: leave test mode and configure before enabling the display. */
    write_register_all(dev, REG_DISPLAYTEST, 0x00);
    write_register_all(dev, REG_SCANLIMIT,   0x07);  /* drive all 8 digits */
    write_register_all(dev, REG_DECODEMODE,  0x00);  /* raw segments, not BCD */
    write_register_all(dev, REG_INTENSITY,
                       (uint8_t)(cfg->intensity > 15 ? 15 : cfg->intensity));
    max7219_clear(dev);
    write_register_all(dev, REG_SHUTDOWN,    0x01);  /* normal operation */

    ESP_LOGI(TAG, "ready: %d module(s) = %dx%d px, clk=%d din=%d cs=%d",
             dev->modules, max7219_width(dev), max7219_height(dev),
             cfg->pin_clk, cfg->pin_din, cfg->pin_cs);

    *out = dev;
    return ESP_OK;

fail_bus:
    spi_bus_free(dev->host);
fail_buf:
    heap_caps_free(dev->txbuf);
    free(dev);
    return err;
}

void max7219_deinit(max7219_dev_t *dev)
{
    if (!dev) {
        return;
    }
    write_register_all(dev, REG_SHUTDOWN, 0x00);
    spi_bus_remove_device(dev->spi);
    spi_bus_free(dev->host);
    heap_caps_free(dev->txbuf);
    free(dev);
}

uint16_t max7219_width(const max7219_dev_t *dev)
{
    return (uint16_t)(dev->modules * MODULE_PX);
}

uint16_t max7219_height(const max7219_dev_t *dev)
{
    return MODULE_PX;
}

esp_err_t max7219_set_intensity(max7219_dev_t *dev, uint8_t intensity)
{
    return write_register_all(dev, REG_INTENSITY, (uint8_t)(intensity > 15 ? 15 : intensity));
}

esp_err_t max7219_set_mapping(max7219_dev_t *dev, max7219_mapping_t mapping)
{
    dev->mapping = mapping;
    return ESP_OK;
}

esp_err_t max7219_clear(max7219_dev_t *dev)
{
    for (int d = 0; d < MODULE_PX; d++) {
        esp_err_t err = write_register_all(dev, (uint8_t)(REG_DIGIT0 + d), 0x00);
        if (err != ESP_OK) {
            return err;
        }
    }
    return ESP_OK;
}

/*
 * Builds the byte that digit register `digit` of module `mod` must receive,
 * reading pixels out of the row-major framebuffer.
 *
 * This function is the entire "transposition layer" the architecture calls
 * for: everything upstream of it, host included, stays panel-agnostic.
 */
static uint8_t pack_digit(const max7219_dev_t *dev, const framebuffer_t *fb,
                          int mod, int digit)
{
    const int x0 = mod * MODULE_PX;
    uint8_t   out = 0;

    for (int k = 0; k < MODULE_PX; k++) {
        bool on;
        int  bit;

        switch (dev->mapping) {
        case MAX7219_MAP_ROW_MAJOR:
            on  = fb_get_pixel(fb, x0 + k, digit);
            bit = 7 - k;
            break;
        case MAX7219_MAP_ROW_MAJOR_REV:
            on  = fb_get_pixel(fb, x0 + k, digit);
            bit = k;
            break;
        case MAX7219_MAP_COL_MAJOR:
            on  = fb_get_pixel(fb, x0 + digit, k);
            bit = 7 - k;
            break;
        case MAX7219_MAP_COL_MAJOR_REV:
        default:
            on  = fb_get_pixel(fb, x0 + digit, k);
            bit = k;
            break;
        }

        if (on) {
            out |= (uint8_t)(1u << bit);
        }
    }
    return out;
}

esp_err_t max7219_render(max7219_dev_t *dev, const framebuffer_t *fb)
{
    uint8_t *row = malloc((size_t)dev->modules);
    if (!row) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t err = ESP_OK;
    for (int digit = 0; digit < MODULE_PX && err == ESP_OK; digit++) {
        for (int mod = 0; mod < dev->modules; mod++) {
            row[mod] = pack_digit(dev, fb, mod, digit);
        }
        err = write_register(dev, (uint8_t)(REG_DIGIT0 + digit), row);
    }

    free(row);
    return err;
}
