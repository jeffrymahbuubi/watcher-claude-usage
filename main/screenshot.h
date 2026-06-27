#pragma once
#include "lvgl.h"

/*
 * Capture each given screen with LVGL's built-in lv_snapshot and stream it over
 * the console UART as RLE-hex framed blocks (@@SHOT name w h / @@END). A host
 * script decodes the frames to PNG — no MCP, no simulator; ground-truth pixels
 * straight off the device. Snapshots are taken under the LVGL lock (fast), then
 * streamed with the lock released so the LVGL task is not starved (WDT-safe).
 */
void screenshot_dump_all(lv_obj_t **scrs, const char **names, int n);
