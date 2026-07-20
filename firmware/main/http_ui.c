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
    /*
     * EMBED_FILES embeds raw bytes with no null terminator - unlike
     * EMBED_TXTFILES - so the length is end-start exactly. Subtracting one
     * here silently truncated the last byte of the page.
     */
    const size_t len = index_html_end - index_html_start;
    ESP_LOGI(TAG, "GET / -> serving %u bytes", (unsigned)len);
    return httpd_resp_send(req, (const char *)index_html_start, len);
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

    char query[96] = {0};
    unsigned w_bytes = 0, h_rows = 0;
    frame_meta_t meta = { .speed_ms = 60 };
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[16];
        if (httpd_query_key_value(query, "w", val, sizeof(val)) == ESP_OK) {
            w_bytes = (unsigned)atoi(val);
        }
        if (httpd_query_key_value(query, "h", val, sizeof(val)) == ESP_OK) {
            h_rows = (unsigned)atoi(val);
        }
        if (httpd_query_key_value(query, "mode", val, sizeof(val)) == ESP_OK) {
            meta.scroll = (strcmp(val, "scroll") == 0);
        }
        if (httpd_query_key_value(query, "dir", val, sizeof(val)) == ESP_OK) {
            meta.rightward = (strcmp(val, "rtl") == 0);
        }
        if (httpd_query_key_value(query, "speed", val, sizeof(val)) == ESP_OK) {
            int sp = atoi(val);
            if (sp >= 10 && sp <= 500) {
                meta.speed_ms = (uint16_t)sp;
            }
        }
    }
    meta.w_bytes = (uint8_t)w_bytes;
    meta.h_rows  = (uint8_t)h_rows;

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

    ctx->cb(payload, &meta, ctx->user);
    free(payload);

    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

/*
 * Captive-portal detection.
 *
 * Each OS fetches its own URL after joining and decides it is behind a portal
 * when the answer is not what it expected. Android wants a bare 204 from
 * /generate_204; iOS wants a page containing "Success"; Windows wants the
 * literal text "Microsoft NCSI". Answering any of them with a 302 instead is
 * what raises the "Sign in to network" banner.
 *
 * These are registered explicitly rather than left to the 404 handler so the
 * behaviour is visible and testable, and so a HEAD probe is handled too.
 */
static esp_err_t portal_redirect(httpd_req_t *req)
{
    ESP_LOGI(TAG, "probe %s -> 302 to portal", req->uri);
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    /* Stops the phone caching "this network is fine" from an earlier join. */
    httpd_resp_set_hdr(req, "Cache-Control", "no-store, must-revalidate");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/* Anything not explicitly routed lands here, which covers probes not listed. */
static esp_err_t redirect_to_root(httpd_req_t *req, httpd_err_code_t err)
{
    (void)err;
    return portal_redirect(req);
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
    cfg.max_uri_handlers = 16;
    /* Phones open several probe connections at once while deciding. */
    cfg.max_open_sockets = 7;

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

    /* Connectivity-probe URLs, newest OS versions first. */
    static const char *probes[] = {
        "/generate_204",            /* Android                     */
        "/gen_204",                 /* Android, older              */
        "/hotspot-detect.html",     /* iOS, macOS                  */
        "/library/test/success.html", /* iOS, older                */
        "/connecttest.txt",         /* Windows 10/11               */
        "/ncsi.txt",                /* Windows, older              */
        "/redirect",                /* Windows                     */
        "/success.txt",             /* Firefox                     */
        "/canonical.html",          /* Ubuntu, GNOME               */
        "/chat",                    /* KDE                         */
    };
    for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
        httpd_uri_t p = {
            .uri = probes[i], .method = HTTP_GET,
            .handler = portal_redirect, .user_ctx = ctx,
        };
        httpd_register_uri_handler(server, &p);
    }
    httpd_register_err_handler(server, HTTPD_404_NOT_FOUND, redirect_to_root);

    ESP_LOGI(TAG, "web UI on http://192.168.4.1/ (panel %ux%u)", panel_w, panel_h);
    return ESP_OK;
}
