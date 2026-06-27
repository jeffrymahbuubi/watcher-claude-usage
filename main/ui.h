#pragma once
#include "claude_usage.h"
#include "claude_status.h"

/* All ui_* calls touch LVGL objects and MUST be made while holding the LVGL
 * port lock (lvgl_port_lock/unlock). */

/* Build the screen widgets. Call once after bsp_lvgl_init(). `brightness` is the
 * persisted LCD brightness (5-100), used to initialise the Settings slider. */
void ui_init(int brightness);

/* Show a standalone first-run setup screen (used in provisioning mode instead
 * of ui_init): instructs the user to join the SoftAP and open the portal. */
void ui_show_setup(const char *ap_ssid);

/* Show a transient status/among message (e.g. "connecting WiFi..."). */
void ui_set_message(const char *msg);

/* Update arcs + labels from a fresh usage snapshot. */
void ui_update(const usage_t *u);

/* Update the SERVICE screen from a fresh claude.com service-health snapshot. */
void ui_update_status(const service_status_t *s);

/* Top-bar WiFi badge: green/connected vs red/offline. */
void ui_set_wifi(bool connected);

/* Mark the on-screen usage as stale (a fetch failed): keep the last-known arcs/%
 * but replace the countdown line with "stale Xm" (age since the last good fetch). */
void ui_set_stale(int age_min);

/* Note: the top-bar battery/USB badge (icon + numeric %) is self-driven by an
 * internal lv_timer (see ui.c power_timer_cb), so it updates on plug/unplug
 * independently of the network poll. No external call is needed. */

/* Capture all screens via lv_snapshot and stream them over UART for the host
 * grab_screens.py decoder (devex / automated visual verification). */
void ui_screenshot(void);

/* Capture just the currently-active screen under the given name (devex). */
void ui_screenshot_one(const char *name);
