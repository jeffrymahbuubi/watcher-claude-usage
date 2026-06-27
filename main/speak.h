#pragma once
#include <stdbool.h>

/* Spoken usage/battery alerts through the BSP speaker (ES8311), offline PCM clips
 * (speak_clips.c). speak_init() must run once after the BSP is up; the check_*
 * helpers are safe no-ops until then. They only enqueue a clip when a metric
 * crosses into a NEW band (5H usage rising / battery falling), with hysteresis,
 * so they can be called on every poll/tick without chattering. Playback runs in
 * its own task, so callers never block. */
void speak_init(void);
void speak_check_usage(int pct_5h);              /* call on each successful 5H usage fetch */
void speak_check_battery(int pct, bool on_usb);  /* call from the 1 s battery timer */
