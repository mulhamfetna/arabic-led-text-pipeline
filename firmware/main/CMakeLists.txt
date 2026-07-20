idf_component_register(
    SRCS
        "main.c"
        "max7219.c"
        "framebuffer.c"
        "canvas.c"
        "frame_rx.c"
        "font5x7.c"
        "text5x7.c"
        "wifi_ap.c"
        "http_ui.c"
    INCLUDE_DIRS "."
    EMBED_FILES "www/index.html"
    REQUIRES
        driver
        esp_wifi
        esp_http_server
        nvs_flash
)
