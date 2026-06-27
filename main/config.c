#include "config.h"
#include "secure_store.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "config";
#define NS "wcfg"

/* Fixed-size secret payload so the encrypted blob length never leaks lengths. */
typedef struct {
    char wifi_pass[CFG_PASS_MAX];
    char token[CFG_TOKEN_MAX];
} secret_blob_t;

bool config_load(config_t *out)
{
    memset(out, 0, sizeof *out);
    out->poll_s = 60;
    out->brightness = 80;

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) != ESP_OK) return false;

    uint8_t prov = 0;
    if (nvs_get_u8(h, "prov", &prov) != ESP_OK || prov != 1) { nvs_close(h); return false; }

    size_t n = sizeof out->ssid;
    nvs_get_str(h, "ssid", out->ssid, &n);
    n = sizeof out->version;
    if (nvs_get_str(h, "ver", out->version, &n) != ESP_OK) strcpy(out->version, "1.0.0");
    int32_t poll = 60;
    nvs_get_i32(h, "poll", &poll);
    out->poll_s = (poll >= 15 && poll <= 3600) ? poll : 60;
    int32_t bri = 80;
    nvs_get_i32(h, "bri", &bri);
    out->brightness = (bri >= 5 && bri <= 100) ? bri : 80;

    /* Decrypt the secret blob. */
    uint8_t enc[sizeof(secret_blob_t) + SECURE_OVERHEAD];
    size_t enc_len = sizeof enc;
    esp_err_t e = nvs_get_blob(h, "sec", enc, &enc_len);
    nvs_close(h);
    if (e != ESP_OK) { ESP_LOGW(TAG, "no secret blob"); return false; }

    secret_blob_t sb;
    size_t plen = 0;
    if (!secure_decrypt(enc, enc_len, (uint8_t *)&sb, sizeof sb, &plen) || plen != sizeof sb) {
        ESP_LOGW(TAG, "secret decrypt failed -> reprovision");
        return false;
    }
    /* Defensive: ensure NUL-termination. */
    sb.wifi_pass[CFG_PASS_MAX - 1] = '\0';
    sb.token[CFG_TOKEN_MAX - 1] = '\0';
    strlcpy(out->wifi_pass, sb.wifi_pass, sizeof out->wifi_pass);
    strlcpy(out->token, sb.token, sizeof out->token);
    memset(&sb, 0, sizeof sb);

    ESP_LOGI(TAG, "loaded config: ssid=\"%s\" ver=%s poll=%ds token=%d chars",
             out->ssid, out->version, out->poll_s, (int)strlen(out->token));
    return out->ssid[0] && out->token[0];
}

bool config_save(const config_t *cfg)
{
    secret_blob_t sb;
    memset(&sb, 0, sizeof sb);
    strlcpy(sb.wifi_pass, cfg->wifi_pass, sizeof sb.wifi_pass);
    strlcpy(sb.token, cfg->token, sizeof sb.token);

    uint8_t enc[sizeof(secret_blob_t) + SECURE_OVERHEAD];
    size_t enc_len = 0;
    bool ok = secure_encrypt((const uint8_t *)&sb, sizeof sb, enc, sizeof enc, &enc_len);
    memset(&sb, 0, sizeof sb);
    if (!ok) { ESP_LOGE(TAG, "encrypt failed"); return false; }

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return false;
    esp_err_t e = ESP_OK;
    e |= nvs_set_str(h, "ssid", cfg->ssid);
    e |= nvs_set_str(h, "ver", cfg->version[0] ? cfg->version : "1.0.0");
    e |= nvs_set_i32(h, "poll", cfg->poll_s >= 15 ? cfg->poll_s : 60);
    e |= nvs_set_i32(h, "bri", (cfg->brightness >= 5 && cfg->brightness <= 100) ? cfg->brightness : 80);
    e |= nvs_set_blob(h, "sec", enc, enc_len);
    e |= nvs_set_u8(h, "prov", 1);
    if (e == ESP_OK) e = nvs_commit(h);
    nvs_close(h);
    memset(enc, 0, sizeof enc);

    if (e != ESP_OK) { ESP_LOGE(TAG, "nvs save failed: %s", esp_err_to_name(e)); return false; }
    ESP_LOGI(TAG, "config saved (ssid=\"%s\")", cfg->ssid);
    return true;
}

void config_factory_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) == ESP_OK) {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
        ESP_LOGW(TAG, "config erased (factory reset)");
    }
}

void config_set_brightness(int brightness)
{
    if (brightness < 5)   brightness = 5;
    if (brightness > 100) brightness = 100;
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    if (nvs_set_i32(h, "bri", brightness) == ESP_OK) nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "brightness persisted: %d%%", brightness);
}
