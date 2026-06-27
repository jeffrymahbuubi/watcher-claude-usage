#include "ui.h"
#include "lvgl.h"
#include "icons_lucide.h"
#include "screenshot.h"
#include "splash.h"
#include "config.h"
#include "speak.h"
#include "sensecap-watcher.h"   /* BSP power/battery: bsp_exp_io_get_level, bsp_battery_get_percent */
#include "esp_system.h"
#include "esp_log.h"
#include <time.h>
#include <string.h>
#include <stdio.h>

/* Extended Montserrat fonts (ASCII + punctuation: curly quotes, en/em dash,
 * ellipsis, middot) generated from the LVGL Montserrat-Medium.ttf via
 * lv_font_conv. Used for text that shows live API/incident strings so UTF-8
 * punctuation renders instead of empty boxes (FR11). */
LV_FONT_DECLARE(font_ext_16);
LV_FONT_DECLARE(font_ext_22);

/* ---- 07-UI-REDESIGN palette (D26–D28). Three lanes kept separate:
 *   chrome = brand terracotta (titles, slider, zap, focus)
 *   data   = success / warning / danger (usage level, battery, service severity)
 *   text   = primary / secondary
 * Restyle the whole UI by swapping these constants. */
#define COL_BRAND     lv_color_hex(0xD97757)   /* terracotta — chrome only, never a data value */
#define COL_TEXT_PRI  lv_color_hex(0xF5F5F5)   /* primary text: hero %, key labels */
#define COL_TEXT_SEC  lv_color_hex(0xC8C8C8)   /* secondary text (replaces the dim PALETTE_GREY) */
#define COL_SUCCESS   lv_color_hex(0x4CAF50)   /* good: usage <70, batt OK, operational glyph */
#define COL_WARNING   lv_color_hex(0xFFC400)   /* caution: usage 70–89, batt 15–49, minor, stale, wifi-down */
#define COL_DANGER    lv_color_hex(0xFF5252)   /* danger: usage >=90, batt <15, major/critical */
#define COL_MAINT     lv_color_hex(0x64B5F6)   /* service maintenance only (the sole reserved blue) */
#define COL_TRACK     lv_color_hex(0x2A2A2A)   /* arc / slider inactive track */

#define NSCR 3   /* 5H / 7D / SERVICE — the top-bar icons live on each screen */

/* Lucide icons are alpha-8bit (monochrome); set their color at runtime via the
 * recolor style. Helper creates one over `parent` tinted `col`. */
static lv_obj_t *new_icon(lv_obj_t *parent, const lv_img_dsc_t *src, lv_color_t col)
{
    lv_obj_t *im = lv_img_create(parent);
    lv_img_set_src(im, src);
    lv_obj_set_style_img_recolor(im, col, 0);
    lv_obj_set_style_img_recolor_opa(im, LV_OPA_COVER, 0);
    return im;
}

static void set_icon(lv_obj_t *im, const lv_img_dsc_t *src, lv_color_t col)
{
    if (!im) return;
    lv_img_set_src(im, src);
    lv_obj_set_style_img_recolor(im, col, 0);
}

/* Three knob-navigable screens: 5h gauge, 7d gauge, service status.
 * The BSP registers the knob as an LVGL ENCODER indev; we add the screen
 * objects to a group bound to that encoder, and load a screen when it gains
 * focus (rotate to cycle). */

typedef struct {
    lv_obj_t *scr;
    lv_obj_t *arc;
    lv_obj_t *pct;
    lv_obj_t *title;
    lv_obj_t *sub;     /* countdown */
    lv_obj_t *zap;     /* gauge-level icon, recolored by level */
} gauge_t;

static gauge_t g5h, g7d;
static lv_obj_t *st_scr, *st_val, *st_sub, *st_health;
static lv_obj_t *anim_scr;   /* FR13 creature-animation screen (4th knob screen) */
static lv_group_t *grp;

/* Settings screen (FR14) + brightness (FR15) + power-off (FR16). The slider and
 * power button are encoder-group members living on set_scr; an internal lv_timer
 * relabels the button Reboot/Power-Off by USB state (see power_timer_cb). */
static lv_obj_t *set_scr;        /* SETTINGS screen */
static lv_obj_t *bri_slider;     /* brightness slider (editable group member) */
static lv_obj_t *bri_val;        /* "NN%" brightness readout */
static lv_obj_t *pwr_btn;        /* power-off button (group member) */
static lv_obj_t *pwr_btn_lbl;    /* its label (relabels "Reboot" while on USB) */
static lv_obj_t *pwr_modal;      /* power-off confirm overlay (top layer) */
static lv_group_t *pwr_modal_grp;
static lv_indev_t *s_enc;        /* encoder indev (for the modal group swap) */
static int s_brightness = 80;    /* slider initial value, from saved config */

/* Top-bar WiFi + battery icons, replicated on each screen (only one shows at a time). */
static lv_obj_t *wifi_ic[NSCR];
static lv_obj_t *batt_ic[NSCR];
static lv_obj_t *batt_lbl[NSCR];   /* numeric % under the battery icon */
static int s_nbar;

static void power_timer_cb(lv_timer_t *t);   /* battery/USB badge updater */

/* WiFi (left) + battery (right) badges in the upper hub.
 * The gauges draw a 270deg ring (outer r=180, inner r=156) whose top arc sits at
 * y~=26..54; placing the icons at the screen's top edge overlapped that ring
 * (T17b). Drop them into the open hub below the inner ring edge (y=64) so they
 * clear the arc on the gauges and stay above the titles on every screen. */
static void add_topbar(lv_obj_t *scr)
{
    int i = s_nbar++;
    if (i >= NSCR) return;
    /* Icons at original 24px; battery % stacked below the icon (D24). The P2
     * enlargement (40px icons + horizontal % cluster) was reverted per user
     * feedback 2026-06-27 — too big. */
    wifi_ic[i] = new_icon(scr, &ic_wifi_off, COL_TEXT_SEC);
    lv_obj_align(wifi_ic[i], LV_ALIGN_TOP_MID, -24, 64);
    batt_ic[i] = new_icon(scr, &ic_batt, COL_TEXT_SEC);
    lv_obj_align(batt_ic[i], LV_ALIGN_TOP_MID, 24, 64);
    batt_lbl[i] = lv_label_create(scr);
    lv_obj_set_style_text_font(batt_lbl[i], &font_ext_22, 0);
    lv_obj_set_style_text_color(batt_lbl[i], COL_TEXT_SEC, 0);
    lv_label_set_text(batt_lbl[i], "--");
    lv_obj_align_to(batt_lbl[i], batt_ic[i], LV_ALIGN_OUT_BOTTOM_MID, 0, 1);
}

static lv_color_t level_color(int pct)
{
    if (pct >= 90) return COL_DANGER;
    if (pct >= 70) return COL_WARNING;
    return COL_SUCCESS;
}

static void screen_focus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_FOCUSED) {
        lv_scr_load_anim(lv_event_get_target(e), LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
    }
}

/* Knob held ~3 s (encoder long-press) -> factory reset + reboot into the setup
 * portal. The long threshold (set in ui_init) keeps brief ENTER presses safe. */
static void reprovision_cb(lv_event_t *e)
{
    (void)e;
    ESP_LOGW("ui", "knob long-press -> factory reset + reprovision");
    config_factory_reset();
    esp_restart();
}

static lv_obj_t *new_screen(void)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, lv_color_black(), 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s, screen_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(s, reprovision_cb, LV_EVENT_LONG_PRESSED, NULL);
    return s;
}

static void build_gauge(gauge_t *g, const char *title, lv_color_t col)
{
    g->scr = new_screen();

    g->arc = lv_arc_create(g->scr);
    lv_obj_set_size(g->arc, 360, 360);
    lv_obj_center(g->arc);
    lv_arc_set_rotation(g->arc, 135);
    lv_arc_set_bg_angles(g->arc, 0, 270);
    lv_arc_set_range(g->arc, 0, 100);
    lv_arc_set_value(g->arc, 0);
    lv_obj_remove_style(g->arc, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(g->arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(g->arc, 24, LV_PART_MAIN);
    lv_obj_set_style_arc_width(g->arc, 24, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(g->arc, COL_TRACK, LV_PART_MAIN);
    lv_obj_set_style_arc_color(g->arc, col, LV_PART_INDICATOR);

    g->title = lv_label_create(g->scr);
    lv_obj_set_style_text_font(g->title, &lv_font_montserrat_28, 0);   /* R5: 22→28 */
    lv_obj_set_style_text_color(g->title, COL_BRAND, 0);               /* D27: all titles terracotta */
    lv_label_set_text(g->title, title);
    lv_obj_align(g->title, LV_ALIGN_CENTER, 0, -66);

    g->pct = lv_label_create(g->scr);
    lv_obj_set_style_text_font(g->pct, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(g->pct, COL_TEXT_PRI, 0);   /* recolored by level on data */
    lv_label_set_text(g->pct, "--%");
    lv_obj_center(g->pct);

    g->sub = lv_label_create(g->scr);
    lv_obj_set_style_text_font(g->sub, &lv_font_montserrat_22, 0);   /* R2: 16→22 */
    lv_obj_set_style_text_color(g->sub, COL_TEXT_SEC, 0);
    lv_label_set_text(g->sub, "");
    lv_obj_align(g->sub, LV_ALIGN_CENTER, 0, 72);

    g->zap = new_icon(g->scr, &ic_zap, COL_BRAND);   /* R4: fixed accent, no longer mirrors level */
    lv_obj_align(g->zap, LV_ALIGN_CENTER, 0, 108);

    add_topbar(g->scr);
}

static void build_status(void)
{
    st_scr = new_screen();

    /* No "SERVICE" title (user pref): the battery % sits below its icon, and the
     * health glyph + description ("All Systems Operational") already identify the
     * screen — a title here only crowds the top bar on the round panel. */
    st_health = new_icon(st_scr, &ic_health_ok, COL_TEXT_SEC);
    lv_obj_align(st_health, LV_ALIGN_CENTER, 0, -48);

    st_val = lv_label_create(st_scr);
    lv_obj_set_style_text_font(st_val, &font_ext_22, 0);
    lv_obj_set_style_text_color(st_val, COL_TEXT_SEC, 0);
    lv_label_set_long_mode(st_val, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(st_val, 280);
    lv_obj_set_style_text_align(st_val, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(st_val, "--");
    lv_obj_align(st_val, LV_ALIGN_CENTER, 0, 6);

    st_sub = lv_label_create(st_scr);
    lv_obj_set_style_text_font(st_sub, &font_ext_16, 0);
    lv_obj_set_style_text_color(st_sub, COL_TEXT_SEC, 0);
    lv_label_set_long_mode(st_sub, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(st_sub, 280);
    lv_obj_set_style_text_align(st_sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(st_sub, "checking...");
    lv_obj_align(st_sub, LV_ALIGN_CENTER, 0, 78);

    add_topbar(st_scr);
}

static void fmt_countdown(char *buf, size_t n, long reset_epoch)
{
    time_t now = time(NULL);
    if ((long)now < 1700000000L || reset_epoch <= 0) { snprintf(buf, n, "resets: --"); return; }
    long d = reset_epoch - (long)now;
    if (d < 0) d = 0;
    long h = d / 3600, m = (d % 3600) / 60;
    if (h >= 24) snprintf(buf, n, "resets in %ldd %ldh", h / 24, h % 24);
    else         snprintf(buf, n, "resets in %ldh %ldm", h, m);
}

/* ---- Settings screen (FR14) + brightness (FR15) + power-off (FR16) ---- */

static void brightness_changed_cb(lv_event_t *e)
{
    (void)e;
    int v = lv_slider_get_value(bri_slider);
    bsp_lcd_brightness_set(v);                          /* live preview */
    if (bri_val) lv_label_set_text_fmt(bri_val, "%d%%", v);
}

static void brightness_defocus_cb(lv_event_t *e)
{
    (void)e;
    /* Persist on edit-exit/blur only: write NVS 'bri' alone, NOT config_save
     * (which would needlessly re-encrypt the whole AES-GCM secret blob). */
    config_set_brightness(lv_slider_get_value(bri_slider));
}

/* Keep the SETTINGS screen visible whenever one of its non-screen group members
 * (slider / power button) is focused — e.g. when the knob wraps straight from 5H
 * to the power button, which would otherwise focus an object on a hidden screen. */
static void settings_member_focus_cb(lv_event_t *e)
{
    (void)e;
    if (lv_scr_act() != set_scr)
        lv_scr_load_anim(set_scr, LV_SCR_LOAD_ANIM_MOVE_LEFT, 250, 0, false);
}

static void modal_close(void)
{
    if (s_enc) lv_indev_set_group(s_enc, grp);
    if (pwr_modal) { lv_obj_del(pwr_modal); pwr_modal = NULL; }
    if (pwr_modal_grp) { lv_group_del(pwr_modal_grp); pwr_modal_grp = NULL; }
}

static void modal_cancel_cb(lv_event_t *e) { (void)e; modal_close(); }

static void modal_confirm_cb(lv_event_t *e)
{
    (void)e;
    /* Re-check at confirm time. Gate on USB (02 §12.2): a true power-off no-ops
     * while the USB rail holds the system up, so reboot instead. */
    bool on_usb = (bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);
    if (on_usb) esp_restart();
    else        bsp_system_shutdown();   /* cut BSP_PWR_SYSTEM; wake = hold wheel ~3s / USB */
}

/* Power-off confirm dialog on the top layer (covers all screens). Cancel is
 * default-focused; the encoder drives a temporary modal group until close. */
static void open_power_confirm(void)
{
    if (pwr_modal) return;
    bool on_usb = (bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);

    pwr_modal = lv_obj_create(lv_layer_top());
    lv_obj_set_size(pwr_modal, 412, 412);
    lv_obj_center(pwr_modal);
    lv_obj_set_style_bg_color(pwr_modal, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(pwr_modal, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(pwr_modal, 0, 0);
    lv_obj_set_style_radius(pwr_modal, 0, 0);
    lv_obj_clear_flag(pwr_modal, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *q = lv_label_create(pwr_modal);
    lv_obj_set_style_text_font(q, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(q, COL_TEXT_PRI, 0);
    lv_label_set_text(q, on_usb ? "Reboot device?" : "Power off?");
    lv_obj_align(q, LV_ALIGN_CENTER, 0, -96);

    lv_obj_t *info = lv_label_create(pwr_modal);
    lv_obj_set_style_text_font(info, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(info, COL_TEXT_SEC, 0);
    lv_label_set_long_mode(info, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(info, 280);
    lv_obj_set_style_text_align(info, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text(info, on_usb ? "On USB power this restarts the device."
                                   : "Hold the wheel ~3s to power on (or connect USB-C).");
    lv_obj_align(info, LV_ALIGN_CENTER, 0, -44);

    lv_obj_t *cancel = lv_btn_create(pwr_modal);
    lv_obj_set_size(cancel, 160, 52);
    lv_obj_align(cancel, LV_ALIGN_CENTER, 0, 36);
    lv_obj_t *cl = lv_label_create(cancel);
    lv_label_set_text(cl, "Cancel");
    lv_obj_center(cl);
    lv_obj_add_event_cb(cancel, modal_cancel_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *ok = lv_btn_create(pwr_modal);
    lv_obj_set_size(ok, 160, 52);
    lv_obj_align(ok, LV_ALIGN_CENTER, 0, 100);
    lv_obj_set_style_bg_color(ok, COL_DANGER, 0);
    lv_obj_t *ol = lv_label_create(ok);
    lv_label_set_text(ol, on_usb ? "Reboot" : "Power Off");
    lv_obj_center(ol);
    lv_obj_add_event_cb(ok, modal_confirm_cb, LV_EVENT_CLICKED, NULL);

    pwr_modal_grp = lv_group_create();
    lv_group_add_obj(pwr_modal_grp, cancel);
    lv_group_add_obj(pwr_modal_grp, ok);
    if (s_enc) lv_indev_set_group(s_enc, pwr_modal_grp);
    lv_group_focus_obj(cancel);   /* Cancel default-focused */
}

static void power_btn_cb(lv_event_t *e) { (void)e; open_power_confirm(); }

static void build_settings(void)
{
    set_scr = new_screen();

    lv_obj_t *t = lv_label_create(set_scr);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_28, 0);   /* R5: 22→28 */
    lv_obj_set_style_text_color(t, COL_BRAND, 0);               /* D27 */
    lv_label_set_text(t, "SETTINGS");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -120);

    lv_obj_t *bl = lv_label_create(set_scr);
    lv_obj_set_style_text_font(bl, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(bl, COL_TEXT_PRI, 0);
    lv_label_set_text(bl, "Brightness");
    lv_obj_align(bl, LV_ALIGN_CENTER, 0, -58);

    bri_slider = lv_slider_create(set_scr);
    lv_slider_set_range(bri_slider, 5, 100);
    lv_slider_set_value(bri_slider, s_brightness, LV_ANIM_OFF);
    lv_obj_set_width(bri_slider, 220);
    lv_obj_align(bri_slider, LV_ALIGN_CENTER, 0, -24);
    lv_obj_set_style_bg_color(bri_slider, COL_TRACK, LV_PART_MAIN);        /* R11: terracotta slider */
    lv_obj_set_style_bg_color(bri_slider, COL_BRAND, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bri_slider, COL_BRAND, LV_PART_KNOB);
    lv_obj_add_event_cb(bri_slider, brightness_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(bri_slider, brightness_defocus_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(bri_slider, settings_member_focus_cb, LV_EVENT_FOCUSED, NULL);

    bri_val = lv_label_create(set_scr);
    lv_obj_set_style_text_font(bri_val, &lv_font_montserrat_22, 0);   /* R2: 16→22 */
    lv_obj_set_style_text_color(bri_val, COL_TEXT_SEC, 0);
    lv_label_set_text_fmt(bri_val, "%d%%", s_brightness);
    lv_obj_align(bri_val, LV_ALIGN_CENTER, 0, 10);

    pwr_btn = lv_btn_create(set_scr);
    lv_obj_set_size(pwr_btn, 200, 56);
    lv_obj_align(pwr_btn, LV_ALIGN_CENTER, 0, 80);
    lv_obj_set_style_bg_color(pwr_btn, COL_DANGER, 0);
    pwr_btn_lbl = lv_label_create(pwr_btn);
    lv_obj_set_style_text_font(pwr_btn_lbl, &lv_font_montserrat_22, 0);
    lv_label_set_text(pwr_btn_lbl, "Power Off");
    lv_obj_center(pwr_btn_lbl);
    lv_obj_add_event_cb(pwr_btn, power_btn_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(pwr_btn, settings_member_focus_cb, LV_EVENT_FOCUSED, NULL);
    /* No add_topbar(): Settings (like CLAWD) carries no wifi/batt badge. */
}

void ui_init(int brightness)
{
    s_brightness = (brightness >= 5 && brightness <= 100) ? brightness : 80;
    build_gauge(&g5h, "5H", lv_palette_main(LV_PALETTE_GREEN));
    build_gauge(&g7d, "7D", lv_palette_main(LV_PALETTE_BLUE));
    build_status();

    /* FR13: creature-animation screen. splash_init() builds its own screen +
     * PSRAM canvas; we give it the same focus-driven load behaviour as the
     * others so the knob can navigate to it. */
    anim_scr = splash_init();
    lv_obj_add_flag(anim_scr, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(anim_scr, screen_focus_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(anim_scr, reprovision_cb, LV_EVENT_LONG_PRESSED, NULL);

    /* Bind the knob (encoder indev) to a group containing the 4 screens. Raise
     * the long-press threshold to ~3 s so the re-provision gesture is deliberate. */
    grp = lv_group_get_default();
    if (grp == NULL) { grp = lv_group_create(); lv_group_set_default(grp); }
    lv_indev_t *idv = NULL;
    while ((idv = lv_indev_get_next(idv)) != NULL) {
        if (idv->driver->type == LV_INDEV_TYPE_ENCODER) {
            lv_indev_set_group(idv, grp);
            idv->driver->long_press_time = 3000;
            s_enc = idv;   /* kept for the power-off modal's group swap */
        }
    }

    build_settings();   /* FR14/15/16: 5th screen + brightness slider + power btn */

    lv_scr_load(g5h.scr);
    lv_group_add_obj(grp, g5h.scr);
    lv_group_add_obj(grp, g7d.scr);
    lv_group_add_obj(grp, st_scr);
    lv_group_add_obj(grp, anim_scr);
    /* Settings is the 5th screen; the slider + power button follow it as members
     * of the same nav ring (D21): rotate cycles 5H,7D,SERVICE,CLAWD,SETTINGS,
     * brightness,power. The slider is editable (press-to-edit); the rest cycle. */
    lv_group_add_obj(grp, set_scr);
    lv_group_add_obj(grp, bri_slider);
    lv_group_add_obj(grp, pwr_btn);

    /* Battery/USB badge updater: 1 s cadence (responsive charging icon) decoupled
     * from the network poll. Paint once now so the boot screenshot has data. */
    lv_timer_create(power_timer_cb, 1000, NULL);
    power_timer_cb(NULL);
}

void ui_set_message(const char *msg)
{
    if (g5h.sub) lv_label_set_text(g5h.sub, msg);
}

void ui_show_setup(const char *ap_ssid)
{
    lv_obj_t *s = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s, lv_color_black(), 0);
    lv_obj_set_style_border_width(s, 0, 0);
    lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *t = lv_label_create(s);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_32, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xD97757), 0);   /* brand terracotta */
    lv_label_set_text(t, "SETUP");
    lv_obj_align(t, LV_ALIGN_CENTER, 0, -120);

    lv_obj_t *body = lv_label_create(s);
    lv_obj_set_style_text_font(body, &lv_font_montserrat_22, 0);
    lv_obj_set_style_text_color(body, lv_color_white(), 0);
    lv_label_set_long_mode(body, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(body, 300);
    lv_obj_set_style_text_align(body, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(body, "1. Join Wi-Fi\n\"%s\"\n\n2. Open\nhttp://192.168.4.1", ap_ssid);
    lv_obj_align(body, LV_ALIGN_CENTER, 0, 24);

    lv_scr_load(s);
}

void ui_screenshot(void)
{
    lv_obj_t *scrs[5] = { g5h.scr, g7d.scr, st_scr, anim_scr, set_scr };
    const char *names[5] = { "5H", "7D", "SERVICE", "CLAWD", "SETTINGS" };
    screenshot_dump_all(scrs, names, 5);
}

void ui_set_stale(int age_min)
{
    char buf[24];
    if (age_min < 60) snprintf(buf, sizeof buf, "stale %dm", age_min);
    else              snprintf(buf, sizeof buf, "stale %dh", age_min / 60);
    if (g5h.sub) { lv_label_set_text(g5h.sub, buf); lv_obj_set_style_text_color(g5h.sub, COL_WARNING, 0); }
    if (g7d.sub) { lv_label_set_text(g7d.sub, buf); lv_obj_set_style_text_color(g7d.sub, COL_WARNING, 0); }
}

void ui_screenshot_one(const char *name)
{
    lv_obj_t *scr = lv_scr_act();
    const char *names[1] = { name };
    screenshot_dump_all(&scr, names, 1);
}

void ui_update(const usage_t *u)
{
    if (!u || !u->ok) { ui_set_message("fetch error"); return; }
    char buf[40];

    lv_arc_set_value(g5h.arc, u->pct_5h);
    lv_obj_set_style_arc_color(g5h.arc, level_color(u->pct_5h), LV_PART_INDICATOR);
    lv_label_set_text_fmt(g5h.pct, "%d%%", u->pct_5h);
    lv_obj_set_style_text_color(g5h.pct, level_color(u->pct_5h), 0);
    fmt_countdown(buf, sizeof buf, u->reset_5h);
    lv_label_set_text(g5h.sub, buf);
    lv_obj_set_style_text_color(g5h.sub, COL_TEXT_SEC, 0);   /* clear any stale tint */

    lv_arc_set_value(g7d.arc, u->pct_7d);
    lv_obj_set_style_arc_color(g7d.arc, level_color(u->pct_7d), LV_PART_INDICATOR);
    lv_label_set_text_fmt(g7d.pct, "%d%%", u->pct_7d);
    lv_obj_set_style_text_color(g7d.pct, level_color(u->pct_7d), 0);
    fmt_countdown(buf, sizeof buf, u->reset_7d);
    lv_label_set_text(g7d.sub, buf);
    lv_obj_set_style_text_color(g7d.sub, COL_TEXT_SEC, 0);

    /* FR13: drive the creature's mood from the worse of the two windows. */
    splash_set_level(u->pct_5h > u->pct_7d ? u->pct_5h : u->pct_7d);
}

/* claude.com indicator → matching Lucide health glyph. */
static const lv_img_dsc_t *health_icon(const char *ind)
{
    if (strcmp(ind, "none") == 0)        return &ic_health_ok;
    if (strcmp(ind, "minor") == 0)       return &ic_health_warn;
    if (strcmp(ind, "major") == 0)       return &ic_health_err;
    if (strcmp(ind, "critical") == 0)    return &ic_health_err;
    if (strcmp(ind, "maintenance") == 0) return &ic_health_maint;
    return &ic_health_warn;   /* unknown/stale */
}

void ui_set_wifi(bool connected)
{
    set_icon(wifi_ic[0], connected ? &ic_wifi : &ic_wifi_off,
             connected ? COL_SUCCESS : COL_WARNING);   /* R4/8.4: disconnect = amber, not red */
    for (int i = 1; i < NSCR; i++)
        set_icon(wifi_ic[i], connected ? &ic_wifi : &ic_wifi_off,
                 connected ? COL_SUCCESS : COL_WARNING);
}

/* Battery/USB badge. Driven by power_timer_cb (an lv_timer) so it tracks
 * plug/unplug promptly, decoupled from the slow network poll loop.
 *  - on_usb (VBUS_IN_DET==0): charging glyph (green).
 *  - on battery: level glyph colored by charge (red<15 / orange<50 / green).
 * A numeric "NN%" sits under the icon (font_ext_16), recolored to match, so the
 * remaining charge is always legible (not just an ambiguous outline). */
static void render_battery(int pct, bool on_usb)
{
    const lv_img_dsc_t *src; lv_color_t col; char txt[16];
    if (on_usb)         { src = &ic_batt_chg;  col = COL_SUCCESS; }
    else if (pct < 0)   { src = &ic_batt;      col = COL_TEXT_SEC; }  /* unknown */
    else if (pct < 15)  { src = &ic_batt_low;  col = COL_DANGER;  }
    else if (pct < 50)  { src = &ic_batt_med;  col = COL_WARNING; }
    else                { src = &ic_batt_full; col = COL_SUCCESS; }
    if (pct < 0) snprintf(txt, sizeof txt, "--");
    else         snprintf(txt, sizeof txt, "%d%%", pct);
    for (int i = 0; i < NSCR; i++) {
        set_icon(batt_ic[i], src, col);
        if (batt_lbl[i]) {
            lv_label_set_text(batt_lbl[i], txt);
            lv_obj_set_style_text_color(batt_lbl[i], col, 0);
            lv_obj_align_to(batt_lbl[i], batt_ic[i], LV_ALIGN_OUT_BOTTOM_MID, 0, 1);
        }
    }
}

/* Reads power state from the BSP. Runs in the LVGL task (lv_timer), so it may
 * touch LVGL objects directly without taking the port lock. USB state is cheap
 * (IO-expander) and read every tick for a responsive charging icon; the battery
 * percent is expensive (ADC x10 + logs) so it's refreshed every ~30 s and
 * immediately on a USB plug/unplug transition (mirrors the stock firmware). */
static void power_timer_cb(lv_timer_t *t)
{
    (void)t;
    static int cached_pct = -1;
    static int last_on_usb = -1;
    static int ticks = 0;
    bool on_usb = (bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET) == 0);
    bool usb_changed = ((int)on_usb != last_on_usb);
    if (cached_pct < 0 || usb_changed || (ticks % 30) == 0)
        cached_pct = (int)bsp_battery_get_percent();
    last_on_usb = on_usb;
    ticks++;
    render_battery(cached_pct, on_usb);
    speak_check_battery(cached_pct, on_usb);   /* spoken battery threshold alerts */
    /* Relabel the Settings power button: a true power-off no-ops on USB, so it
     * acts as Reboot while plugged in (02 §12.2). */
    if (pwr_btn_lbl) lv_label_set_text(pwr_btn_lbl, on_usb ? "Reboot" : "Power Off");
}

/* claude.com health → color: operational green, degraded orange, outage red. */
static lv_color_t indicator_color(const char *ind)
{
    if (strcmp(ind, "none") == 0)     return COL_SUCCESS;
    if (strcmp(ind, "critical") == 0) return COL_DANGER;
    if (strcmp(ind, "major") == 0)    return COL_DANGER;
    if (strcmp(ind, "minor") == 0)    return COL_WARNING;
    if (strcmp(ind, "maintenance") == 0) return COL_MAINT;
    return COL_WARNING;   /* unknown/stale — caution, distinct from valid-secondary grey */
}

void ui_update_status(const service_status_t *s)
{
    if (!s || !s->ok) {
        lv_label_set_text(st_val, "status\nunavailable");
        lv_obj_set_style_text_color(st_val, COL_TEXT_SEC, 0);
        lv_label_set_text(st_sub, "");
        set_icon(st_health, &ic_health_err, COL_DANGER);   /* glyph shape + colour agree */
        return;
    }

    lv_color_t col = indicator_color(s->indicator);
    set_icon(st_health, health_icon(s->indicator), col);
    lv_label_set_text(st_val, s->description);   /* extended font → raw UTF-8 OK */
    lv_obj_set_style_text_color(st_val, col, 0);

    if (s->incident_count > 0 && s->incident[0]) {
        if (s->incident_count > 1)
            lv_label_set_text_fmt(st_sub, "%s (+%d more)", s->incident, s->incident_count - 1);
        else
            lv_label_set_text(st_sub, s->incident);
        /* R3/§8.1: an incident is bad news — never inherit the operational-green indicator.
         * Floor at amber; escalate to danger for major/critical. */
        bool severe = (strcmp(s->indicator, "major") == 0 || strcmp(s->indicator, "critical") == 0);
        lv_obj_set_style_text_color(st_sub, severe ? COL_DANGER : COL_WARNING, 0);
    } else {
        lv_label_set_text(st_sub, "no incidents");
        lv_obj_set_style_text_color(st_sub, COL_TEXT_SEC, 0);
    }
}
