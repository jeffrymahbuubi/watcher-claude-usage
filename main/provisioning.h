#pragma once
#include <stdbool.h>
#include "config.h"

/* First-run captive-portal provisioning (P6 / T6).
 *
 * Brings up a SoftAP ("Watcher-Setup"), a captive DNS responder, and an HTTP
 * server that serves a setup form (WiFi SSID/password + Claude OAuth token +
 * claude-code version + poll interval). Blocks until the form is submitted,
 * fills *out, and returns true. The caller then config_save()s and reboots.
 *
 * Call after esp_netif_init() + esp_event_loop_create_default(). */
bool provisioning_run(config_t *out);

/* The SoftAP SSID a user connects their phone to. */
#define PROV_AP_SSID "Watcher-Setup"
