#include "screenshot.h"
#include "sensecap-watcher.h"   /* lvgl_port_lock / lvgl_port_unlock */
#include "esp_heap_caps.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdint.h>

#define MAX_SCR 8

/* Stream one RGB565 snapshot as RLE-hex: per run, 4 hex chars count + 4 hex
 * chars raw 16-bit pixel value (as stored in the framebuffer; the host un-swaps
 * for LV_COLOR_16_SWAP). Framed by @@SHOT / @@END marker lines. */
static void stream_frame(const char *name, lv_img_dsc_t *d)
{
    static const char HEX[] = "0123456789ABCDEF";
    const uint16_t *px = (const uint16_t *)d->data;
    int n = (int)d->header.w * (int)d->header.h;

    printf("\n@@SHOT %s %d %d\n", name, (int)d->header.w, (int)d->header.h);

    /* static: keep this 1 KB off the (small) caller stack; dump is single-threaded. */
    static char ob[1024];
    int op = 0, col = 0, i = 0;
    while (i < n) {
        uint16_t v = px[i];
        uint32_t c = 1;
        while (i + (int)c < n && px[i + c] == v && c < 0xFFFF) c++;
        uint16_t cc = (uint16_t)c;
        ob[op++] = HEX[(cc >> 12) & 0xF]; ob[op++] = HEX[(cc >> 8) & 0xF];
        ob[op++] = HEX[(cc >>  4) & 0xF]; ob[op++] = HEX[cc & 0xF];
        ob[op++] = HEX[(v  >> 12) & 0xF]; ob[op++] = HEX[(v  >> 8) & 0xF];
        ob[op++] = HEX[(v  >>  4) & 0xF]; ob[op++] = HEX[v  & 0xF];
        i += c;
        if (++col >= 64) { ob[op++] = '\n'; col = 0; }
        if (op >= 1000) { fwrite(ob, 1, op, stdout); op = 0; }
    }
    if (op) fwrite(ob, 1, op, stdout);
    printf("\n@@END\n");
    fflush(stdout);
}

void screenshot_dump_all(lv_obj_t **scrs, const char **names, int n)
{
    if (n > MAX_SCR) n = MAX_SCR;
    uint8_t *bufs[MAX_SCR] = {0};
    lv_img_dsc_t dsc[MAX_SCR];

    /* Capture phase — fast, under the LVGL lock. */
    lvgl_port_lock(0);
    for (int i = 0; i < n; i++) {
        lv_obj_update_layout(scrs[i]);
        uint32_t need = lv_snapshot_buf_size_needed(scrs[i], LV_IMG_CF_TRUE_COLOR);
        bufs[i] = heap_caps_malloc(need, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!bufs[i]) continue;
        if (lv_snapshot_take_to_buf(scrs[i], LV_IMG_CF_TRUE_COLOR, &dsc[i], bufs[i], need) != LV_RES_OK) {
            heap_caps_free(bufs[i]);
            bufs[i] = NULL;
        }
    }
    lvgl_port_unlock();

    /* Stream phase — slow, lock released so the LVGL task keeps running. Mute
     * logs so they don't interleave with the binary-ish hex stream. */
    esp_log_level_set("*", ESP_LOG_NONE);
    for (int i = 0; i < n; i++) {
        if (bufs[i]) { stream_frame(names[i], &dsc[i]); heap_caps_free(bufs[i]); }
    }
    esp_log_level_set("*", ESP_LOG_INFO);
}
