#include "http_get.h"

#include <string.h>
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"

static const char *TAG = "https";

typedef struct {
    char *buf;
    int   cap;   /* total buffer size */
    int   len;   /* bytes written so far */
} capture_t;

static esp_err_t on_evt(esp_http_client_event_t *e)
{
    if (e->event_id == HTTP_EVENT_ON_DATA && e->user_data) {
        capture_t *c = (capture_t *)e->user_data;
        int room = c->cap - 1 - c->len;          /* leave space for NUL */
        int cp = (e->data_len < room) ? e->data_len : room;
        if (cp > 0) {
            memcpy(c->buf + c->len, e->data, cp);
            c->len += cp;
        }
    }
    return ESP_OK;
}

int https_get(const char *url, const http_header_t *headers, int nheaders,
              char *out, int out_size)
{
    capture_t cap = { .buf = out, .cap = out_size, .len = 0 };
    if (out && out_size > 0) {
        out[0] = '\0';
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,   /* validate cert vs CA bundle */
        .event_handler = on_evt,
        .user_data = (out && out_size > 0) ? &cap : NULL,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client == NULL) {
        ESP_LOGE(TAG, "client init failed");
        return -1;
    }

    for (int i = 0; i < nheaders; i++) {
        esp_http_client_set_header(client, headers[i].name, headers[i].value);
    }

    int status = -1;
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        status = esp_http_client_get_status_code(client);
        if (out && out_size > 0) {
            out[cap.len] = '\0';
        }
        ESP_LOGI(TAG, "GET %s -> HTTP %d (%d bytes)", url, status, cap.len);
    } else {
        ESP_LOGE(TAG, "GET %s failed: %s", url, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return status;
}
