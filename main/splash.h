#pragma once
#include "lvgl.h"

/*
 * Creature-animation screen (FR13 / T18). A 20x20 palette-indexed pixel-art
 * engine ported from the Clawdmeter reference; frames come from the vendored
 * claudepix data (splash_animations.h). The animation's mood follows the live
 * usage level: idle (<25%) -> normal -> active -> heavy/frantic (>=80%).
 *
 * All calls touch LVGL objects and MUST be made while holding the LVGL port
 * lock (the lv_timer callback runs inside the LVGL task, so it is already safe).
 */

/* Build the animation screen + canvas (PSRAM) and start the frame timer.
 * Returns the screen object so the caller can add it to the knob group. */
lv_obj_t *splash_init(void);

/* Drive the mood from the latest usage level (0-100 = max(pct_5h, pct_7d)). */
void splash_set_level(int pct);
