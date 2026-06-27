#include "provisioning.h"
#include "wifi_connect.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "prov";

static SemaphoreHandle_t s_done;
static config_t *s_out;
static httpd_handle_t s_httpd;

/* ---- minimal captive DNS: answer every A query with 192.168.4.1 ---- */
static void dns_task(void *arg)
{
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) { ESP_LOGE(TAG, "dns socket failed"); vTaskDelete(NULL); return; }
    struct sockaddr_in sa = { .sin_family = AF_INET, .sin_port = htons(53),
                              .sin_addr.s_addr = htonl(INADDR_ANY) };
    if (bind(sock, (struct sockaddr *)&sa, sizeof sa) < 0) {
        ESP_LOGE(TAG, "dns bind failed"); close(sock); vTaskDelete(NULL); return;
    }
    uint8_t buf[512];
    while (1) {
        struct sockaddr_in src; socklen_t sl = sizeof src;
        int n = recvfrom(sock, buf, sizeof buf, 0, (struct sockaddr *)&src, &sl);
        if (n < 12) continue;
        /* Build an answer: echo header+question, flags=0x8180, 1 answer A=192.168.4.1 */
        buf[2] = 0x81; buf[3] = 0x80;          /* response, recursion available */
        buf[6] = 0; buf[7] = 1;                /* ANCOUNT = 1 */
        buf[8] = buf[9] = buf[10] = buf[11] = 0;
        if (n + 16 > (int)sizeof buf) continue;
        uint8_t *p = buf + n;
        *p++ = 0xC0; *p++ = 0x0C;              /* name ptr -> question */
        *p++ = 0x00; *p++ = 0x01;              /* type A */
        *p++ = 0x00; *p++ = 0x01;              /* class IN */
        *p++ = 0x00; *p++ = 0x00; *p++ = 0x00; *p++ = 0x3C;  /* TTL 60 */
        *p++ = 0x00; *p++ = 0x04;              /* RDLENGTH 4 */
        *p++ = 192; *p++ = 168; *p++ = 4; *p++ = 1;
        sendto(sock, buf, p - buf, 0, (struct sockaddr *)&src, sl);
    }
}

/* ---- form HTML ---- */
static const char FORM[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<title>Watcher Setup</title><style>body{font-family:sans-serif;max-width:480px;margin:24px auto;"
"padding:0 16px;background:#1a1a1a;color:#eee}h2{color:#d97757}label{display:block;margin:14px 0 4px}"
"input{width:100%;padding:10px;border:1px solid #444;border-radius:6px;background:#222;color:#eee;"
"box-sizing:border-box}button{margin-top:20px;width:100%;padding:12px;background:#d97757;color:#fff;"
"border:0;border-radius:6px;font-size:16px}small{color:#999}</style></head><body>"
"<h2>Claude Watcher Setup</h2><form method=POST action=/save>"
"<label>WiFi network (2.4 GHz)</label><input name=ssid required>"
"<label>WiFi password</label><input name=pass type=password>"
"<label>Claude OAuth token <small>(claude setup-token)</small></label><input name=token required>"
"<label>claude --version <small>(optional)</small></label><input name=ver value='1.0.0'>"
"<label>Poll interval (s)</label><input name=poll type=number value=60 min=15 max=3600>"
"<label>Brightness (%)</label><input name=bri type=number value=80 min=5 max=100>"
"<button type=submit>Save &amp; reboot</button></form></body></html>";

static const char DONE[] =
"<!doctype html><html><head><meta charset='utf-8'>"
"<meta name=viewport content='width=device-width,initial-scale=1'>"
"<style>body{font-family:sans-serif;max-width:480px;margin:48px auto;text-align:center;"
"background:#1a1a1a;color:#eee}h2{color:#d97757}</style></head><body>"
"<h2>Saved \xe2\x9c\x93</h2><p>The Watcher is rebooting and will connect to your WiFi.</p>"
"</body></html>";

static esp_err_t form_get(httpd_req_t *r)
{
    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_send(r, FORM, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/* In-place URL-decode (%XX and '+'). */
static void urldecode(char *s)
{
    char *d = s;
    for (char *p = s; *p; p++) {
        if (*p == '+') { *d++ = ' '; }
        else if (*p == '%' && p[1] && p[2]) {
            int hi = p[1], lo = p[2];
            hi = (hi <= '9') ? hi - '0' : (hi | 0x20) - 'a' + 10;
            lo = (lo <= '9') ? lo - '0' : (lo | 0x20) - 'a' + 10;
            *d++ = (char)((hi << 4) | lo); p += 2;
        } else { *d++ = *p; }
    }
    *d = '\0';
}

static void get_field(const char *body, const char *key, char *out, size_t cap)
{
    out[0] = '\0';
    if (httpd_query_key_value(body, key, out, cap) == ESP_OK) urldecode(out);
}

static esp_err_t save_post(httpd_req_t *r)
{
    int len = r->content_len;
    if (len <= 0 || len > 4000) { httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "bad"); return ESP_FAIL; }
    char *body = malloc(len + 1);
    if (!body) { httpd_resp_send_err(r, HTTPD_500_INTERNAL_SERVER_ERROR, "oom"); return ESP_FAIL; }
    int got = 0;
    while (got < len) {
        int rd = httpd_req_recv(r, body + got, len - got);
        if (rd <= 0) { free(body); return ESP_FAIL; }
        got += rd;
    }
    body[len] = '\0';

    char poll[12], bri[12];
    get_field(body, "ssid",  s_out->ssid,      sizeof s_out->ssid);
    get_field(body, "pass",  s_out->wifi_pass, sizeof s_out->wifi_pass);
    get_field(body, "token", s_out->token,     sizeof s_out->token);
    get_field(body, "ver",   s_out->version,   sizeof s_out->version);
    get_field(body, "poll",  poll,             sizeof poll);
    get_field(body, "bri",   bri,              sizeof bri);
    s_out->poll_s = poll[0] ? atoi(poll) : 60;
    s_out->brightness = bri[0] ? atoi(bri) : 80;
    free(body);

    if (!s_out->ssid[0] || !s_out->token[0]) {
        httpd_resp_send_err(r, HTTPD_400_BAD_REQUEST, "ssid + token required");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "form: ssid=\"%s\" token=%d chars ver=%s poll=%d",
             s_out->ssid, (int)strlen(s_out->token), s_out->version, s_out->poll_s);

    httpd_resp_set_type(r, "text/html; charset=utf-8");
    httpd_resp_send(r, DONE, HTTPD_RESP_USE_STRLEN);
    xSemaphoreGive(s_done);   /* wake provisioning_run() */
    return ESP_OK;
}

bool provisioning_run(config_t *out)
{
    s_out = out;
    s_done = xSemaphoreCreateBinary();

    if (wifi_ap_start(PROV_AP_SSID) != ESP_OK) return false;
    xTaskCreate(dns_task, "dns", 3072, NULL, 4, NULL);

    httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
    hc.uri_match_fn = httpd_uri_match_wildcard;
    hc.lru_purge_enable = true;
    if (httpd_start(&s_httpd, &hc) != ESP_OK) { ESP_LOGE(TAG, "httpd start failed"); return false; }

    httpd_uri_t save = { .uri = "/save", .method = HTTP_POST, .handler = save_post };
    httpd_uri_t any  = { .uri = "/*",    .method = HTTP_GET,  .handler = form_get };
    httpd_register_uri_handler(s_httpd, &save);
    httpd_register_uri_handler(s_httpd, &any);   /* catch-all -> serve form (captive) */

    ESP_LOGI(TAG, "provisioning: join WiFi \"%s\", open http://192.168.4.1", PROV_AP_SSID);

    /* Block until the form posts. Give the browser a moment to fetch the DONE
     * page before we let the caller reboot. */
    xSemaphoreTake(s_done, portMAX_DELAY);
    vTaskDelay(pdMS_TO_TICKS(1500));
    httpd_stop(s_httpd);
    return true;
}
