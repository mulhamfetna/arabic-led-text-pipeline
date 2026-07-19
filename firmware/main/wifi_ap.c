/*
 * arabic-led-text-pipeline - SoftAP + captive portal DNS
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "wifi_ap.h"

#include <string.h>
#include <sys/socket.h>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "wifi_ap";

#define DNS_PORT      53
#define DNS_MAX_LEN   512
#define AP_IP_OCTETS  { 192, 168, 4, 1 }

/* ---------------------------------------------------------------- DNS ---- */

typedef struct __attribute__((packed)) {
    uint16_t id;
    uint16_t flags;
    uint16_t qd_count;
    uint16_t an_count;
    uint16_t ns_count;
    uint16_t ar_count;
} dns_header_t;

/*
 * Answers every A query with our own address. This is what turns "joined a
 * network" into "phone opens a browser by itself": the OS probes a known URL,
 * gets us instead of the expected reply, and concludes it is behind a portal.
 */
static void dns_task(void *arg)
{
    (void)arg;
    uint8_t buf[DNS_MAX_LEN];

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) {
        ESP_LOGE(TAG, "dns socket failed");
        vTaskDelete(NULL);
        return;
    }

    struct sockaddr_in bind_addr = {
        .sin_family      = AF_INET,
        .sin_port        = htons(DNS_PORT),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    if (bind(sock, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) < 0) {
        ESP_LOGE(TAG, "dns bind failed");
        close(sock);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "captive DNS listening on :53");

    for (;;) {
        struct sockaddr_in src;
        socklen_t src_len = sizeof(src);
        int len = recvfrom(sock, buf, sizeof(buf), 0, (struct sockaddr *)&src, &src_len);

        if (len < (int)sizeof(dns_header_t)) {
            continue;
        }

        dns_header_t *hdr = (dns_header_t *)buf;
        if (ntohs(hdr->qd_count) != 1) {
            continue;   /* not worth handling multi-question queries here */
        }

        /* Walk the QNAME label sequence to find where the question ends. */
        int p = sizeof(dns_header_t);
        while (p < len && buf[p] != 0) {
            p += buf[p] + 1;
        }
        p += 1 + 4;     /* terminating zero + QTYPE + QCLASS */
        if (p > len || p + 16 > (int)sizeof(buf)) {
            continue;
        }

        hdr->flags    = htons(0x8180);  /* response, recursion available */
        hdr->an_count = htons(1);
        hdr->ns_count = 0;
        hdr->ar_count = 0;

        /* Answer: pointer back to the question's name, A/IN, TTL 60, our IP. */
        const uint8_t answer[] = {
            0xC0, 0x0C,                 /* name is a pointer to offset 12   */
            0x00, 0x01, 0x00, 0x01,     /* TYPE=A, CLASS=IN                 */
            0x00, 0x00, 0x00, 0x3C,     /* TTL = 60s                        */
            0x00, 0x04,                 /* RDLENGTH = 4                     */
            192, 168, 4, 1,
        };
        memcpy(buf + p, answer, sizeof(answer));

        sendto(sock, buf, p + sizeof(answer), 0, (struct sockaddr *)&src, src_len);
    }
}

/* ---------------------------------------------------------------- AP ----- */

static void on_wifi_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg; (void)base;
    if (id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *e = data;
        ESP_LOGI(TAG, "phone joined: " MACSTR, MAC2STR(e->mac));
    } else if (id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *e = data;
        ESP_LOGI(TAG, "phone left: " MACSTR, MAC2STR(e->mac));
    }
}

esp_err_t wifi_ap_start(const char *ssid, const char *password)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init_cfg));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi_event, NULL, NULL));

    wifi_config_t cfg = {
        .ap = {
            .max_connection = 4,
            .authmode       = WIFI_AUTH_WPA2_PSK,
            .channel        = 1,
        },
    };
    strlcpy((char *)cfg.ap.ssid, ssid, sizeof(cfg.ap.ssid));
    cfg.ap.ssid_len = strlen(ssid);

    /*
     * A password shorter than 8 characters is not a weak password, it is an
     * invalid one - WPA2 rejects it. Fall back to an open network rather than
     * failing to start, since this AP carries nothing sensitive.
     */
    if (password && strlen(password) >= 8) {
        strlcpy((char *)cfg.ap.password, password, sizeof(cfg.ap.password));
    } else {
        cfg.ap.authmode = WIFI_AUTH_OPEN;
        if (password && password[0]) {
            ESP_LOGW(TAG, "password shorter than 8 chars, starting open instead");
        }
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &cfg));

    /*
     * The banner comes from two mechanisms working together:
     *
     *   1. DNS hijack - every lookup resolves to us (see dns_task).
     *   2. Probe-URL redirects - the OS fetches its connectivity-check URL,
     *      gets a 302 instead of the expected reply, and concludes it is
     *      behind a portal (see http_ui.c).
     *
     * This is the same approach MikroTik and hotel portals use, and it is what
     * produces the tappable "Sign in to network" notification.
     *
     * RFC 8910 DHCP option 114 would announce the portal URL directly and more
     * reliably on iOS 14+/Android 11+, but ESP_NETIF_CAPTIVEPORTAL_URI only
     * exists from ESP-IDF v5.4 and this builds against v5.3.2. Worth revisiting
     * on an IDF bump.
     */
    (void)ap_netif;

    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP \"%s\" up (%s), http://192.168.4.1/",
             ssid, cfg.ap.authmode == WIFI_AUTH_OPEN ? "open" : "WPA2");

    xTaskCreate(dns_task, "captive_dns", 4096, NULL, 4, NULL);
    return ESP_OK;
}
