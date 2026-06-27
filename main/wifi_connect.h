#pragma once
#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/*
 * WiFi bring-up. Assumes esp_netif_init() + esp_event_loop_create_default()
 * have already been called. A given boot uses exactly one mode: STA for normal
 * operation, or SoftAP for first-run provisioning (after which we esp_restart).
 */

/* Connect as STA to the given network; block until an IP is obtained or timeout.
 * Empty/NULL pass => open network. Returns ESP_OK on success, ESP_FAIL otherwise. */
esp_err_t wifi_sta_connect(const char *ssid, const char *pass, uint32_t timeout_ms);

/* Start a SoftAP (open, for the captive setup portal). ap_ssid is the broadcast
 * name. Returns ESP_OK once the AP + DHCP server are up. */
esp_err_t wifi_ap_start(const char *ap_ssid);

/* Current STA link state (true between GOT_IP and a later DISCONNECTED). Used by
 * the UI to show offline vs merely-stale. */
bool wifi_is_connected(void);
