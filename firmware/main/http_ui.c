/*
 * arabic-led-text-pipeline - HTTP UI and frame endpoint
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#include "http_ui.h"

#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "http_ui";

/* index.html is linked in by EMBED_FILES; see main/CMakeLists.txt. */
extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");

typedef struct {
    uint16_t         panel_w;
    uint16_t         panel_h;
    http_frame_cb_t  cb;
    void            *user;
} ui_ctx_t;

static esp_err_t index_get(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, (const char *)index_html_start,
                           index_html_end - index_html_start - 1);
}

/* Lets the page size its canvas to the real panel instead of guessing. */
static esp_err_t panel_get(httpd_req_t *req)
{
    ui_ctx_t *ctx = req->user_ctx;
    char body[64];
    int n = snprintf(body, sizeof(body), "{\"width\":%u,\"height\":%u}",
                     ctx->panel_w, ctx->panel_h);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, body, n);
}

/*
 * POST /frame?w=<bytes>&h=<rows> with the packed payload as the raw body.
 *
 * No CRC here: TCP already checksums, and a corrupt body would fail the length
 * check below. The UART path keeps its CRC because a bare serial line has no
 * integrity check of its own.
 */
static esp_err_t frame_post(httpd_req_t *req)
{
    ui_ctx_t *ctx = req->user_ctx;

    char query[64] = {0};
    unsigned w_bytes = 0, h_rows = 0;
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8];
        if (httpd_query_key_value(query, "w", val, sizeof(val)) == ESP_OK) {
            w_bytes = (unsigned)atoi(val);
        }
        if (httpd_query_key_value(query, "h", val, sizeof(val)) == ESP_OK) {
            h_rows = (unsigned)atoi(val);
        }
    }

    const size_t expected = (size_t)w_bytes * h_rows;
    if (expected == 0 || expected > CONFIG_FRAME_MAX_PAYLOAD) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad geometry");
        return ESP_FAIL;
    }
    if ((size_t)req->content_len != expected) {
        ESP_LOGW(TAG, "body %d bytes, geometry implies %u",
                 req->content_len, (unsigned)expected);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "length mismatch");
        return ESP_FAIL;
    }

    uint8_t *payload = malloc(expected);
    if (!payload) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }

    size_t got = 0;
    while (got < expected) {
        int n = httpd_req_recv(req, (char *)payload + got, expected - got);
        if (n <= 0) {
            free(payload);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "short body");
            return ESP_FAIL;
        }
        got += (size_t)n;
    }

    ctx->cb(payload, (uint8_t)w_bytes, (uint8_t)h_rows, ctx->user);
    free(payload);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

/*
 * Every OS probes a different URL to detect a captive portal, and none of them
 * agree. Redirecting all unknown paths to the root covers the lot without
 * hardcoding Apple's, Google's and Microsoft's individual endpoints.
 */
static esp_err_t redirect_to_root(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t http_ui_start(uint16_t panel_w, uint16_t panel_h,
                        http_frame_cb_t cb, void *user)
{
    ui_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        return ESP_ERR_NO_MEM;
    }
    ctx->panel_w = panel_w;
    ctx->panel_h = panel_h;
    ctx->cb      = cb;
    ctx->user    = user;

    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.lru_purge_enable = true;
    cfg.max_uri_handlers = 8;

    httpd_handle_t server = NULL;
    esp_err_t err = httpd_start(&server, &cfg);
    if (err != ESP_OK) {
        free(ctx);
        return err;
    }

    const httpd_uri_t routes[] = {
        { .uri = "/",       .method = HTTP_GET,  .handler = index_get, .user_ctx = ctx },
        { .uri = "/panel",  .method = HTTP_GET,  .handler = panel_get, .user_ctx = ctx },
        { .uri = "/frame",  .method = HTTP_POST, .handler = frame_post, .user_ctx = ctx },
    };
    for (size_t i = 0; i < sizeof(routes) / sizeof(routes[0]); i++) {
        httpd_register_uri_handler(server, &routes[i]);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_to_root);

    ESP_LOGI(TAG, "web UI on http://192.168.4.1/ (panel %ux%u)", panel_w, panel_h);
    return ESP_OK;
}
