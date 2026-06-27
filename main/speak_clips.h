#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    SPK_USAGE_25,
    SPK_USAGE_50,
    SPK_USAGE_75,
    SPK_USAGE_90,
    SPK_USAGE_100,
    SPK_BATT_50,
    SPK_BATT_30,
    SPK_BATT_20,
    SPK_BATT_10,
    SPK_BATT_5,
    SPK_COUNT
} speak_clip_id_t;

typedef struct { const int16_t *pcm; size_t samples; } speak_clip_t;
extern const speak_clip_t SPEAK_CLIPS[SPK_COUNT];
#define SPEAK_SAMPLE_RATE 16000
