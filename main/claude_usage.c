#include "claude_usage.h"

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "usage";

/* Auth provided at runtime (from config / provisioning) — no compiled secrets. */
static char s_auth_hdr[320];   /* "Bearer <token>" */
static char s_ua_hdr[64];      /* "claude-code/<version>" */

void claude_usage_set_auth(const char *token, const char *version)
{
    snprintf(s_auth_hdr, sizeof s_auth_hdr, "Bearer %s", token ? token : "");
    snprintf(s_ua_hdr, sizeof s_ua_hdr, "claude-code/%s", (version && version[0]) ? version : "1.0.0");
}

#define MESSAGES_URL "https://api.anthropic.com/v1/messages"
#define PROBE_BODY \
    "{\"model\":\"claude-haiku-4-5-20251001\",\"max_tokens\":1," \
    "\"messages\":[{\"role\":\"user\",\"content\":\"hi\"}]}"

#define BODY_MAX 512
static char s_body[BODY_MAX + 1];
static int  s_blen;

static char s_5h_util[24], s_5h_reset[40];
static char s_7d_util[24], s_7d_reset[40];
static char s_status[24];

static esp_err_t evt(esp_http_client_event_t *e)
{
    switch (e->event_id) {
    case HTTP_EVENT_ON_HEADER:
        if (strncasecmp(e->header_key, "anthropic-ratelimit", 19) == 0) {
            if (strcasecmp(e->header_key, "anthropic-ratelimit-unified-5h-utilization") == 0)
                strlcpy(s_5h_util, e->header_value, sizeof(s_5h_util));
            else if (strcasecmp(e->header_key, "anthropic-ratelimit-unified-5h-reset") == 0)
                strlcpy(s_5h_reset, e->header_value, sizeof(s_5h_reset));
            else if (strcasecmp(e->header_key, "anthropic-ratelimit-unified-7d-utilization") == 0)
                strlcpy(s_7d_util, e->header_value, sizeof(s_7d_util));
            else if (strcasecmp(e->header_key, "anthropic-ratelimit-unified-7d-reset") == 0)
                strlcpy(s_7d_reset, e->header_value, sizeof(s_7d_reset));
            else if (strcasecmp(e->header_key, "anthropic-ratelimit-unified-status") == 0)
                strlcpy(s_status, e->header_value, sizeof(s_status));
        }
        break;
    case HTTP_EVENT_ON_DATA: {
        int room = BODY_MAX - s_blen;
        int cp = (e->data_len < room) ? e->data_len : room;
        if (cp > 0) { memcpy(s_body + s_blen, e->data, cp); s_blen += cp; }
        break;
    }
    default:
        break;
    }
    return ESP_OK;
}

bool claude_usage_fetch(usage_t *out)
{
    s_blen = 0; s_body[0] = '\0';
    s_5h_util[0] = s_5h_reset[0] = s_7d_util[0] = s_7d_reset[0] = s_status[0] = '\0';
    memset(out, 0, sizeof(*out));

    esp_http_client_config_t cfg = {
        .url = MESSAGES_URL,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = evt,
    };
    if (s_auth_hdr[0] == '\0') { ESP_LOGE(TAG, "no auth set"); return false; }

    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (c == NULL) { ESP_LOGE(TAG, "client init failed"); return false; }

    esp_http_client_set_header(c, "Authorization",    s_auth_hdr);
    esp_http_client_set_header(c, "anthropic-beta",    "oauth-2025-04-20");
    esp_http_client_set_header(c, "anthropic-version", "2023-06-01");
    esp_http_client_set_header(c, "User-Agent",        s_ua_hdr);
    esp_http_client_set_header(c, "content-type",      "application/json");
    esp_http_client_set_post_field(c, PROBE_BODY, strlen(PROBE_BODY));

    esp_err_t err = esp_http_client_perform(c);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(c);
        out->http_status = status;
        s_body[s_blen] = '\0';
        if (status == 200 && (s_5h_util[0] || s_7d_util[0])) {
            out->pct_5h  = (int)(atof(s_5h_util) * 100.0 + 0.5);
            out->pct_7d  = (int)(atof(s_7d_util) * 100.0 + 0.5);
            out->reset_5h = atol(s_5h_reset);
            out->reset_7d = atol(s_7d_reset);
            strlcpy(out->status, s_status[0] ? s_status : "allowed", sizeof(out->status));
            out->ok = true;
            ESP_LOGI(TAG, "5h %d%% (reset %ld), 7d %d%%, status %s",
                     out->pct_5h, out->reset_5h, out->pct_7d, out->status);
        } else {
            ESP_LOGW(TAG, "HTTP %d, no usable headers; body: %s", status, s_body);
        }
    } else {
        ESP_LOGE(TAG, "probe failed: %s", esp_err_to_name(err));
    }

    esp_http_client_cleanup(c);
    return out->ok;
}
