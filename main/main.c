/*
 * Watcher Claude Usage Display.
 * Boot: bring up display/LVGL, load config from NVS. If unprovisioned, run the
 * captive-portal setup (SoftAP + web form) then reboot. Otherwise connect WiFi
 * with the stored creds, decrypt the OAuth token, and poll usage + service
 * health every config interval, rendering on the round LVGL screens.
 */
#include <assert.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"

#include "sensecap-watcher.h"   /* BSP: bsp_*, lvgl_port_*, esp_io_expander */
#include "ui.h"
#include "wifi_connect.h"
#include "claude_usage.h"
#include "claude_status.h"
#include "config.h"
#include "provisioning.h"
#include "speak.h"

#define P6_SEED_FROM_SECRETS 0   /* TEMP: set 0 for the clean shipping build */
#if P6_SEED_FROM_SECRETS
#include "secrets.h"
#endif

static const char *TAG = "main";

static void run_provisioning(void)
{
    config_t cfg;
    if (lvgl_port_lock(0)) { ui_show_setup(PROV_AP_SSID); lvgl_port_unlock(); }
    bsp_rgb_set(40, 0, 40);   /* magenta: setup mode */
    vTaskDelay(pdMS_TO_TICKS(800));
    ui_screenshot_one("SETUP");   /* devex: capture the portal screen on boot */

    if (provisioning_run(&cfg) && config_save(&cfg)) {
        ESP_LOGI(TAG, "provisioned -> rebooting");
    } else {
        ESP_LOGE(TAG, "provisioning failed");
    }
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "nvs_flash_init -> %s; erasing and retrying", esp_err_to_name(ret));
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_LOGI(TAG, "=== Watcher Claude Usage : P6 ===");

    /* Display + IO expander + RGB LED via the BSP. */
    esp_io_expander_handle_t io = bsp_io_expander_init();
    assert(io != NULL);
    bsp_rgb_init();
    lv_disp_t *disp = bsp_lvgl_init();
    assert(disp != NULL);

    config_t cfg;
    bool provisioned = config_load(&cfg);

    /* DEF2 (D25): deliberately NO boot-time knob factory-reset. The hold-wheel-to-
     * wake gesture (after a true power-off, T23/D20) stays held through boot and used
     * to collide with this check, wiping the NVS creds and forcing WiFi re-login.
     * Reprovision is now ONLY via the in-use ~3 s encoder long-press (ui.c
     * reprovision_cb), which cannot be triggered accidentally on wake. */

#if P6_SEED_FROM_SECRETS   /* TEMP verification: seed NVS from secrets.h, bypass portal */
    if (!provisioned) {
        strlcpy(cfg.ssid, WIFI_SSID, sizeof cfg.ssid);
        strlcpy(cfg.wifi_pass, WIFI_PASSWORD, sizeof cfg.wifi_pass);
        strlcpy(cfg.token, CLAUDE_OAUTH_TOKEN, sizeof cfg.token);
        strlcpy(cfg.version, CLAUDE_CODE_VERSION, sizeof cfg.version);
        cfg.poll_s = 60;
        if (config_save(&cfg)) {
            config_t v; if (config_load(&v)) { cfg = v; provisioned = true;
                ESP_LOGW(TAG, "TEMP: seeded+reloaded config (crypto roundtrip OK)"); }
        }
    }
#endif

    if (!provisioned) {
        run_provisioning();   /* never returns (reboots) */
        return;
    }

    /* Provisioned: normal operation. */
    if (lvgl_port_lock(0)) {
        ui_init(cfg.brightness);
        ui_set_message("connecting WiFi...");
        lvgl_port_unlock();
    }
    claude_usage_set_auth(cfg.token, cfg.version);
    bsp_lcd_brightness_set(cfg.brightness);   /* P7: configurable static brightness */
    speak_init();             /* spoken usage/battery alerts via the ES8311 speaker */
    bsp_rgb_set(0, 0, 40);   /* blue: connecting */

    /* One-shot connect; the WiFi handler auto-reconnects forever in the
     * background across outages, so the poll loop just renders offline/stale
     * until the link returns. (Re-provision via the knob to change creds.) */
    wifi_sta_connect(cfg.ssid, cfg.wifi_pass, 20000);

    /* Time sync, for reset countdowns. Non-fatal; syncs later once online. */
    esp_sntp_config_t sntp_cfg = ESP_NETIF_SNTP_DEFAULT_CONFIG("pool.ntp.org");
    esp_netif_sntp_init(&sntp_cfg);
    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) == ESP_OK)
        ESP_LOGI(TAG, "time synced (NTP)");
    else
        ESP_LOGW(TAG, "SNTP sync timed out; countdowns will show '--'");

    /* Entering steady operation: turn the RGB LED OFF to save battery — the screen
     * already shows usage level + status, so the always-on LED was redundant drain.
     * Boot/setup/connecting colors above are kept as brief power-on cues. (User
     * decision 2026-06-27.) */
    bsp_rgb_set(0, 0, 0);

    /* P7: poll loop with battery, freshness/stale, and 429 backoff. */
    const int base_s = cfg.poll_s;
    const int max_backoff_s = 600;   /* cap 429 backoff at 10 min */
    int interval_s = base_s;
    int64_t last_ok_us = esp_timer_get_time();
    bool ever_ok = false;
    bool shot_done = false;

    while (1) {
        usage_t u;
        bool ok = claude_usage_fetch(&u);

        service_status_t svc;
        bool svc_ok = claude_status_fetch(&svc);

        if (ok) { last_ok_us = esp_timer_get_time(); ever_ok = true; }
        int age_min = (int)((esp_timer_get_time() - last_ok_us) / 60000000LL);
        bool online = wifi_is_connected();

        /* DEF1 (02 §12.1, D19): the battery/USB badge is now self-driven by an
         * lv_timer in ui.c (power_timer_cb) which reads BSP_PWR_VBUS_IN_DET +
         * battery percent on a 1 s cadence, so plug/unplug updates promptly
         * instead of waiting for this (slow, backoff-stretched) poll loop. */
        if (lvgl_port_lock(0)) {
            ui_set_wifi(online);
            if (ok) {
                ui_update(&u);
            } else if (ever_ok) {
                ui_set_stale(age_min);          /* keep last-known arcs/% */
            } else {
                ui_set_message(online ? "fetching..." : "offline");
            }
            if (svc_ok) ui_update_status(&svc);  /* else keep last-known health */
            lvgl_port_unlock();
        }

        if (!shot_done) { shot_done = true; ui_screenshot(); }

        /* 429 backoff only. The steady-state RGB level cue (green/amber/red by usage)
         * was REMOVED (user decision 2026-06-27): the on-screen arc + % + zap already
         * convey the level, so an always-on LED was pure redundant battery drain. The
         * LED stays OFF while running (set off once before this loop). */
        if (ok) {
            interval_s = base_s;
            speak_check_usage(u.pct_5h);   /* spoken 5H-usage threshold alerts */
        } else if (u.http_status == 429) {
            interval_s = (interval_s * 2 > max_backoff_s) ? max_backoff_s : interval_s * 2;
            ESP_LOGW(TAG, "HTTP 429 -> backoff to %ds", interval_s);
        }
        vTaskDelay(pdMS_TO_TICKS(interval_s * 1000));
    }
}
