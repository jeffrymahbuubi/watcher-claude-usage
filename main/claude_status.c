#include "claude_status.h"
#include "http_get.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"

static const char *TAG = "status";

/* Atlassian Statuspage summary: overall `status` + `incidents` (unresolved). */
#define SUMMARY_URL "https://status.claude.com/api/v2/summary.json"
#define BODY_CAP    6144   /* summary.json is a few KB; allocate in PSRAM */

bool claude_status_fetch(service_status_t *out)
{
    memset(out, 0, sizeof(*out));

    char *body = heap_caps_malloc(BODY_CAP, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (body == NULL) body = malloc(BODY_CAP);   /* fall back to internal RAM */
    if (body == NULL) { ESP_LOGE(TAG, "no memory for body"); return false; }

    int status = https_get(SUMMARY_URL, NULL, 0, body, BODY_CAP);
    if (status != 200) {
        ESP_LOGW(TAG, "summary.json -> HTTP %d", status);
        free(body);
        return false;
    }

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) { ESP_LOGW(TAG, "JSON parse failed"); return false; }

    cJSON *st = cJSON_GetObjectItem(root, "status");
    if (cJSON_IsObject(st)) {
        cJSON *ind = cJSON_GetObjectItem(st, "indicator");
        cJSON *des = cJSON_GetObjectItem(st, "description");
        if (cJSON_IsString(ind)) strlcpy(out->indicator, ind->valuestring, sizeof(out->indicator));
        if (cJSON_IsString(des)) strlcpy(out->description, des->valuestring, sizeof(out->description));
    }

    cJSON *incs = cJSON_GetObjectItem(root, "incidents");
    if (cJSON_IsArray(incs)) {
        out->incident_count = cJSON_GetArraySize(incs);
        cJSON *first = cJSON_GetArrayItem(incs, 0);
        if (cJSON_IsObject(first)) {
            cJSON *name = cJSON_GetObjectItem(first, "name");
            if (cJSON_IsString(name)) strlcpy(out->incident, name->valuestring, sizeof(out->incident));
        }
    }

    cJSON_Delete(root);

    if (out->indicator[0] == '\0') strlcpy(out->indicator, "unknown", sizeof(out->indicator));
    if (out->description[0] == '\0') strlcpy(out->description, out->indicator, sizeof(out->description));
    out->ok = true;
    ESP_LOGI(TAG, "indicator=%s desc=\"%s\" incidents=%d%s%s",
             out->indicator, out->description, out->incident_count,
             out->incident[0] ? " first=" : "", out->incident);
    return true;
}
