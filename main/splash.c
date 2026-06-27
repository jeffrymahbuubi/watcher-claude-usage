#include "splash.h"
#include "splash_animations.h"
#include "esp_heap_caps.h"
#include <string.h>

/* 20x20 grid, à la Clawdmeter. On the round 412x412 panel CELL = 412/20 = 20,
 * giving a 400x400 canvas centred inside the circle (the empty corners fall
 * outside the round mask — they are palette[0] = black anyway). */
#define GRID        20
#define CELL        20
#define CANVAS_W    (GRID * CELL)   /* 400 */
#define CANVAS_H    (GRID * CELL)   /* 400 */

/* Auto-cycle to the next animation in the current mood group this often. */
#define ROTATE_INTERVAL_MS 20000

/* Usage-level mood groups (4 levels x up to 4 animations), resolved to indices
 * into splash_anims[] by name at init. Mirrors the Clawdmeter grouping. */
#define GROUP_COUNT 4
#define GROUP_MAX   4

static const char *GROUP_NAMES[GROUP_COUNT][GROUP_MAX] = {
    /* 0 — idle / sleepy  (<25%) */
    { "expression sleep", "idle breathe", "idle blink", "expression wink" },
    /* 1 — normal pace    (25-50%) */
    { "idle look around", "work think", "work coding", NULL },
    /* 2 — active         (50-80%) */
    { "dance sway", "expression surprise", "dance bounce", NULL },
    /* 3 — heavy / frantic (>=80%) */
    { "dance bounce dj", "dance sway dj", "dance djmix", NULL },
};

static int8_t  group_lists[GROUP_COUNT][GROUP_MAX];
static uint8_t group_size[GROUP_COUNT];
static uint8_t group_rotation[GROUP_COUNT];

static lv_obj_t   *s_scr     = NULL;
static lv_obj_t   *s_canvas  = NULL;
static lv_color_t *s_buf     = NULL;          /* CANVAS_W*CANVAS_H, PSRAM */
static lv_timer_t *s_timer   = NULL;

static int      s_group     = 0;
static uint16_t s_anim      = 0;
static uint16_t s_frame     = 0;
static uint32_t s_frame_ms  = 0;              /* lv_tick at current frame start */
static uint32_t s_pick_ms   = 0;              /* lv_tick at last anim pick */

/* Per-animation 10-entry colour LUT, rebuilt on each pick. The stored palette
 * is standard RGB565; lv_color_make() produces the right in-memory layout for
 * whatever LV_COLOR_16_SWAP is set to, so this is byte-order agnostic. */
static lv_color_t s_lut[SPLASH_PALETTE_SIZE];

static lv_color_t rgb565_to_color(uint16_t v)
{
    uint8_t r = (v >> 11) & 0x1F, g = (v >> 5) & 0x3F, b = v & 0x1F;
    return lv_color_make((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2));
}

static void build_lut(const uint16_t *palette)
{
    for (int i = 0; i < SPLASH_PALETTE_SIZE; i++) s_lut[i] = rgb565_to_color(palette[i]);
}

static void render_frame(const uint8_t *cells)
{
    if (!s_buf) return;
    for (int gy = 0; gy < GRID; gy++) {
        /* Fill one grid-row's worth of source pixels into the first cell-rows,
         * then memcpy that row to the remaining (cell-1) sub-rows. */
        lv_color_t *row = &s_buf[(gy * CELL) * CANVAS_W];
        for (int gx = 0; gx < GRID; gx++) {
            uint8_t code = cells[gy * GRID + gx];
            lv_color_t col = (code < SPLASH_PALETTE_SIZE) ? s_lut[code] : s_lut[0];
            lv_color_t *p = &row[gx * CELL];
            for (int i = 0; i < CELL; i++) p[i] = col;
        }
        for (int dy = 1; dy < CELL; dy++)
            memcpy(&s_buf[(gy * CELL + dy) * CANVAS_W], row, CANVAS_W * sizeof(lv_color_t));
    }
    if (s_canvas) lv_obj_invalidate(s_canvas);
}

static void load_anim(int idx)
{
    if (idx < 0 || idx >= SPLASH_ANIM_COUNT) return;
    s_anim  = (uint16_t)idx;
    s_frame = 0;
    s_frame_ms = s_pick_ms = lv_tick_get();
    build_lut(splash_anims[s_anim].palette);
    render_frame(splash_anims[s_anim].frames[0]);
}

/* Pick the next animation in the current mood group (round-robin). */
static void pick_for_group(void)
{
    int g = s_group;
    if (g < 0 || g >= GROUP_COUNT || group_size[g] == 0) g = 0;
    if (group_size[g] == 0) return;            /* nothing resolved (shouldn't happen) */
    uint8_t slot = group_rotation[g] % group_size[g];
    group_rotation[g]++;
    load_anim(group_lists[g][slot]);
}

static void resolve_groups(void)
{
    for (int g = 0; g < GROUP_COUNT; g++) {
        group_size[g] = 0;
        group_rotation[g] = 0;
        for (int s = 0; s < GROUP_MAX; s++) {
            group_lists[g][s] = -1;
            const char *want = GROUP_NAMES[g][s];
            if (!want) continue;
            for (int i = 0; i < SPLASH_ANIM_COUNT; i++) {
                if (strcmp(splash_anims[i].name, want) == 0) {
                    group_lists[g][group_size[g]++] = (int8_t)i;
                    break;
                }
            }
        }
    }
}

/* Frame/rotation timer. Runs in the LVGL task (lock held), so touching objects
 * is safe. Animates only while the splash screen is actually showing. */
static void splash_timer_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_buf || SPLASH_ANIM_COUNT == 0) return;
    if (lv_scr_act() != s_scr) return;         /* off-screen: don't burn CPU */

    if (lv_tick_elaps(s_pick_ms) >= ROTATE_INTERVAL_MS) pick_for_group();

    const splash_anim_def_t *a = &splash_anims[s_anim];
    if (a->frame_count == 0) return;
    uint16_t hold = a->holds[s_frame];
    if (hold < 20) hold = 20;                   /* clamp absurdly-fast frames */
    if (lv_tick_elaps(s_frame_ms) >= hold) {
        s_frame = (s_frame + 1) % a->frame_count;
        s_frame_ms = lv_tick_get();
        render_frame(a->frames[s_frame]);
    }
}

lv_obj_t *splash_init(void)
{
    resolve_groups();

    s_buf = heap_caps_malloc(CANVAS_W * CANVAS_H * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);

    s_scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_scr, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_scr, 0, 0);
    lv_obj_set_style_pad_all(s_scr, 0, 0);
    lv_obj_clear_flag(s_scr, LV_OBJ_FLAG_SCROLLABLE);

    if (s_buf) {
        s_canvas = lv_canvas_create(s_scr);
        lv_canvas_set_buffer(s_canvas, s_buf, CANVAS_W, CANVAS_H, LV_IMG_CF_TRUE_COLOR);
        lv_obj_center(s_canvas);
        lv_canvas_fill_bg(s_canvas, lv_color_black(), LV_OPA_COVER);
        if (SPLASH_ANIM_COUNT > 0) load_anim(group_lists[0][0] >= 0 ? group_lists[0][0] : 0);
    } else {
        /* PSRAM alloc failed — show a label instead of a blank screen. */
        lv_obj_t *l = lv_label_create(s_scr);
        lv_label_set_text(l, "splash: no buffer");
        lv_obj_set_style_text_color(l, lv_palette_main(LV_PALETTE_GREY), 0);
        lv_obj_center(l);
    }

    s_timer = lv_timer_create(splash_timer_cb, 33, NULL);   /* ~30 Hz check */
    return s_scr;
}

void splash_set_level(int pct)
{
    int g = (pct < 25) ? 0 : (pct < 50) ? 1 : (pct < 80) ? 2 : 3;
    if (g == s_group) return;                  /* same mood — let it keep cycling */
    s_group = g;
    pick_for_group();                          /* switch group immediately */
}
