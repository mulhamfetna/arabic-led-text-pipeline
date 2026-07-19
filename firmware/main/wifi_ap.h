/*
 * arabic-led-text-pipeline - SoftAP + captive portal DNS
 * Copyright (C) 2026  Mulham Fetna
 * SPDX-License-Identifier: AGPL-3.0-or-later
 */
#pragma once

#include "esp_err.h"

/*
 * Brings up an open access point at 192.168.4.1 and starts a DNS responder
 * that answers every A query with that address, so the phone's connectivity
 * check fails in the specific way that makes it pop up the captive portal.
 */
esp_err_t wifi_ap_start(const char *ssid, const char *password);
