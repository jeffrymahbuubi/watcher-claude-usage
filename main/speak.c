#include "speak.h"
#include "speak_clips.h"
#include "sensecap-watcher.h"        /* bsp_codec_init / bsp_codec_set_fs / bsp_codec_volume_set / bsp_i2s_write */
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"

static const char *TAG = "speak";

#define SPEAK_BOOT_TEST 0   /* Audio path confirmed (user heard it 2026-06-27). No boot announcement —
                             * the device speaks only on real threshold crossings. */

/* Announce thresholds. Usage rises into 25/50/75/90/100; battery falls into 50/30/20/10/5.
 * (User decision 2026-06-27; see 07/SPEC.) */
static const int USAGE_TH[5] = { 25, 50, 75, 90, 100 };
static const int BATT_TH[5]  = { 50, 30, 20, 10, 5 };
#define HYST 3   /* % a metric must back off a boundary before that band re-arms (anti-chatter) */

static QueueHandle_t s_q;
static bool s_ready;

static void enqueue(int id);

#define SPEAK_CHUNK 1024   /* samples per I2S write (~64 ms @16k) */

static void speak_task(void *arg)
{
    (void)arg;
    int id;
    for (;;) {
        if (xQueueReceive(s_q, &id, portMAX_DELAY) != pdTRUE) continue;
        if (id < 0 || id >= SPK_COUNT) continue;
        const speak_clip_t *c = &SPEAK_CLIPS[id];
        /* Write in small chunks (like the BSP's openai-realtime example), NOT one
         * giant buffer: a single ~100 KB esp_codec_dev_write spun and tripped the
         * Task-WDT. Each chunk blocks ~64 ms on the I2S DMA, yielding between writes. */
        for (size_t off = 0; off < c->samples; off += SPEAK_CHUNK) {
            size_t n = c->samples - off;
            if (n > SPEAK_CHUNK) n = SPEAK_CHUNK;
            size_t written = 0;
            if (bsp_i2s_write((void *)(c->pcm + off), n * sizeof(int16_t), &written, 1000) != ESP_OK)
                break;
        }
    }
}

void speak_init(void)
{
    if (s_ready) return;
    /* bsp_codec_init() already opens the codec at DRV_AUDIO_SAMPLE_RATE/BITS/CHANNELS =
     * 16000 / 16 / mono — exactly our clip format. Do NOT call bsp_codec_set_fs again:
     * the redundant close/reopen left the play channel disabled and esp_codec_dev_write
     * then spun (Task-WDT timeout). */
    if (bsp_codec_init() != ESP_OK) { ESP_LOGE(TAG, "bsp_codec_init failed"); return; }
    int vol = 0;
    bsp_codec_volume_set(95, &vol);                                /* max (BSP caps at 95) — user wants it loud */
    bsp_codec_mute_set(false);                                     /* clear any power-on mute (per the BSP example) */
    s_q = xQueueCreate(4, sizeof(int));
    if (!s_q) { ESP_LOGE(TAG, "queue alloc failed"); return; }
    xTaskCreate(speak_task, "speak", 4096, NULL, 4, NULL);
    s_ready = true;
    ESP_LOGI(TAG, "speak ready @ %d Hz", SPEAK_SAMPLE_RATE);
#if SPEAK_BOOT_TEST
    enqueue(SPK_USAGE_50);   /* one-shot audio-path check at boot */
#endif
}

static void enqueue(int id)
{
    if (s_ready && s_q) xQueueSend(s_q, &id, 0);   /* non-blocking; drop if a clip is already playing */
}

/* 5H usage: announce when it RISES into a higher band. */
void speak_check_usage(int pct)
{
    static int last = -1;
    int band = 0;
    for (int i = 0; i < 5; i++) if (pct >= USAGE_TH[i]) band = i + 1;
    if (last < 0) { last = band; return; }                          /* prime silently on first reading */
    if (band > last) { enqueue(SPK_USAGE_25 + (band - 1)); last = band; }
    else if (last > 0 && pct < USAGE_TH[last - 1] - HYST) last--;    /* re-arm one band once it backs off */
}

/* Battery: announce when it FALLS into a lower band, only while on battery. */
void speak_check_battery(int pct, bool on_usb)
{
    static int last = -1;
    static bool was_usb = true;
    if (on_usb || pct < 0) { was_usb = true; return; }              /* charging/unknown: hold, no alerts */
    int band = 0;
    for (int i = 0; i < 5; i++) if (pct <= BATT_TH[i]) band = i + 1;
    if (last < 0 || was_usb) { last = band; was_usb = false; return; }   /* prime silently on unplug/boot */
    if (band > last) { enqueue(SPK_BATT_50 + (band - 1)); last = band; }
    else if (last > 0 && pct > BATT_TH[last - 1] + HYST) last--;
}
