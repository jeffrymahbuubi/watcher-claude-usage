# 07 — UI Redesign: Legibility & Semantic Restyle

> **Status: 🟢 P1 LIVE — P2 (size enlargement) REVERTED per user preference (2026-06-27).**
> **P1 is the live UI:** semantic palette, terracotta titles @28, brighter+bigger secondary text,
> incident-green→amber fix, fixed-accent zap, terracotta slider. **P2 (R6 icon enlargement 24→40 / 30→80,
> R9 `font_ext_28` status text, D29 horizontal %) was implemented, shown, then REVERTED — the user found
> the bigger icons/text too large.** Back to original icon sizes (24/30), status font (22/16), and battery
> % below the icon (D24). See **§13** (P1 as-built), **§14** (P2, now reverted), **§15** (revert / D30).
> **Optional remaining (P3):** R8 status-word, R10 fetch-error degraded look, R11 minor layout, 5 % brightness floor.
> **Last updated:** 2026-06-27 · Parent: [SPEC.md](SPEC.md) · Touches `ui.c`, `icons_lucide.*`, `font_ext_*`, `sdkconfig`, `tools/icontool/`.

---

## 1. Goal & Context

**Trigger.** 2026-06-27 UI review of the as-built UI (all 5 screens captured live off the device
via `tools/grab_screens.py`, post-P9/D24 layout) by a 4-lens agent panel: legibility/accessibility,
visual hierarchy/brand, embedded-LVGL feasibility, status-semantics correctness.

**User goals (locked, 2026-06-27):**
- The device is read **BOTH** at arm's length (~30–50 cm) **AND** glanceably from across the room
  (1–3 m) → optimize for a strong **size hierarchy + high contrast**.
- **Moderate redesign:** keep the dark/black theme, but introduce a coherent **semantic color
  system**, a real **type scale**, and reclaim the **empty lower half** of the round panel.
- Focus dimensions the user named: **icon size, icon color, text size, text color** (plus the
  layout/semantic consequences those force).

**Baseline.** Current screens (pre-redesign) are in [`docs/screenshots/`](../screenshots/) — note those
on-disk PNGs predate this review; the 2026-06-27 captures used for this spec live in the session
scratchpad. Re-capture after implementation for a before/after.

## 2. Core problems (review consensus)

Up close the UI is fine; against the **both-distances + correct-meaning** bar, three issues converge:

1. **The hero *symbols* are 3–4× too small for room distance.** Icons are 24–30 px native
   (≈4 arcmin @ 2 m; simple-glyph recognition needs ~12–15 arcmin). On SERVICE the health glyph —
   the most meaningful symbol — is a near-invisible dot, so the *text* carries the status instead.
2. **Everything secondary is too small *and* too dim.** Countdown, battery %, and most SETTINGS text
   are 16 px `LV_PALETTE_GREY` (`#9E9E9E`, ≈6.4:1) — first to vanish at distance.
3. **Color carries meaning alone, and sometimes the *wrong* meaning.** The usage ramp is hue-only
   (colour-blind-unsafe; hue desaturates first at distance), and the SERVICE incident line inherits
   the operational **green** — a "we've suspended access…" message literally painted as "all good."

**Bright spot to emulate:** the **battery badge** (`render_battery`, `ui.c:524-542`) is already
triple-encoded — glyph *shape* + colour *tier* + numeric *%* — i.e. colour-blind-safe. Bring SERVICE
and the gauges **up to** that standard.

## 3. Decisions (locked this session)

Logged in [SPEC.md §4](SPEC.md) as **D26–D28**:
- **D26** — Adopt this UI-redesign initiative (both-distances legibility + moderate semantic restyle,
  dark theme kept). Spec = this doc.
- **D27** — **All screen titles = brand terracotta `#D97757`** (5H, 7D, SERVICE, SETTINGS). Supersedes
  the 5H-green / 7D-blue title colours. **Blue is reserved exclusively for the `maintenance` service
  state.** Rationale: cleanly separate *chrome* (terracotta) from *data* (green/amber/red ramp) from
  *text* (white/grey) so no colour does five jobs.
- **D28** — **Icon scale = "moderate":** top-bar WiFi/battery 24→**~44–48 px**, SERVICE health glyph
  30→**~80 px**, via **regenerating** the bitmaps crisp (`gen_icons.mjs`), not runtime zoom. Chosen
  over the aggressive 64–72 px option (room-optimal but crowds the hub / collides with the ring) and
  over cheap `lv_img_set_zoom` (soft edges on thin glyphs).

Everything in §4–§8 below is **proposed within those locked directions**, pending your review of this
doc before implementation.

## 4. Semantic colour palette (proposed)

Replace ad-hoc `lv_palette_*` calls with named constants. Three lanes — **chrome / data / text** —
never cross them.

| Role | Hex | Replaces | Where (ui.c) |
|---|---|---|---|
| **Primary text** | `#F5F5F5` near-white | `lv_color_white()` | hero `g->pct`, "Brightness" label |
| **Secondary text** | `#C8C8C8` (bright, ≈11:1) | `LV_PALETTE_GREY` `#9E9E9E` | `g->sub`, `batt_lbl`, `st_val`/`st_sub` (valid states), `bri_val`, SETTINGS title |
| **Brand accent (chrome)** | `#D97757` terracotta | green/blue **titles** | all titles; slider handle/indicator; focus ring; the zap (now decorative) |
| **Success (data)** | `#4CAF50` | `LV_PALETTE_GREEN` | usage <70, WiFi-up, batt-OK, service-operational **glyph only** |
| **Warning (data)** | `#FFC400` amber-yellow | `LV_PALETTE_ORANGE` | usage 70–89, batt 15–49, minor incident **glyph only** |
| **Danger (data)** | `#FF5252` | `LV_PALETTE_RED` | usage ≥90, batt <15, major/critical, Power-Off button |
| **Maintenance (data)** | `#2196F3`/`#64B5F6` blue | (kept) | service `maintenance` **only** |
| **Unknown / stale** | dim amber + non-colour badge | overloaded grey | distinct from valid-secondary grey (see §8.2) |
| **Track / inactive** | `#2A2A2A` | arc bg `#303030` (`ui.c:152`) | arc track, slider track |

**Lane rule (the single biggest coherence win):**
- **Terracotta = "this is chrome / a control."** Titles, slider, focus, zap. **Never** a data value.
- **Green→amber→red = the data ramp.** Usage level, battery tier, service severity. **Never** chrome.
- **White/grey = text.** Carries *information*, not status — see the SERVICE fix in §8.1.

Amber chosen `#FFC400` (lighter than the old orange) so warning separates from success by **luminance**,
not just hue (colour-blind safety). Danger `#FF5252` lighter than `#F44336` for the same reason.

## 5. Type scale (proposed)

Four steps. Montserrat is linked at **12/14/16/22/32/48 (already compiled in)**; **24 and 28 require a
one-line `sdkconfig` flip** (see §10). Live status/incident strings must stay on the `font_ext_*` family
(they carry the curly-quote/em-dash glyphs; a built-in Montserrat would re-introduce the FR11 boxes).

| Token | Font | Use | Status |
|---|---|---|---|
| **Display** | `montserrat_48` | hero `g->pct` (5H/7D only — keep unique to gauges) | compiled |
| **Title** | `montserrat_28` (or 24) | "5H", "7D", "SERVICE", "SETTINGS" (bump from 22) | **enable in sdkconfig** |
| **Body** | `font_ext_22` | SERVICE description `st_val`, modal question, SETUP body | compiled |
| **Caption** | `montserrat_22` / `font_ext_22` | **countdown `g->sub`** + **battery % `batt_lbl`** (bump from 16) | compiled |
| **Micro** | `font_ext_16` | benign "no incidents", least-important labels only | compiled |

Per-screen mapping in §7. Note: the countdown/battery bump to 22 is **free** (already compiled); the
title bump to 24/28 is the only type cost.

## 6. Icon plan (proposed)

Native sizes (`icons_lucide.c`, `LV_IMG_CF_ALPHA_8BIT`, recoloured at runtime): WiFi/battery/zap = **24×24**;
health glyphs = **30×30**. Colour is 100 % runtime (`lv_obj_set_style_img_recolor`, `ui.c:25-39`), so a
palette swap is a constant change.

**Size (D28 — regenerate crisp via `gen_icons.mjs`):**
| Icon | Now | Proposed | Note |
|---|---|---|---|
| SERVICE health glyph `st_health` | 30 | **~80 px** | the screen's headline symbol; must read across the room |
| top-bar WiFi/battery | 24 | **~44–48 px** | re-tune `add_topbar` y-offset to clear the ring (see ⚠) |
| zap `g->zap` | 24 | **drop or keep ~24 as fixed terracotta accent** | currently redundant (see §8.5) |

**Colour:**
- Resting WiFi/battery use **secondary `#C8C8C8`**, not grey-idle, so they're legible before first data.
- Alert states keep a **shape change** (e.g. `wifi`↔`wifi_off`, battery tier glyphs) so colour is never
  the *only* cue (colour-blind safety).
- WiFi-disconnect → **grey/dim-amber, not red** (§8.4).

⚠ **Arc-collision gotcha** (`ui.c:81-85`): the 360 px ring's top arc sits at y≈26–54, which is exactly
why the top-bar icons were dropped to y=64. Enlarging them to ~48 px will re-encroach — **re-verify the
offset clears the ring on the gauge screens** after the change.

## 7. Per-screen layout (proposed)

Round 412×412, centre (206,206). Reclaim the empty lower third.

### 7.1 — 5H / 7D gauges (`build_gauge`, `ui.c:137-177`)
| Element | Now | Proposed |
|---|---|---|
| Title `g->title` | montserrat_22, green (5H) / blue (7D), `CENTER 0,-66` | **montserrat_28, terracotta `#D97757`** (both) |
| Hero `g->pct` | montserrat_48, white→level | keep size; white→**level (success/warn/danger)**; add **status word** below (§12) |
| Countdown `g->sub` | montserrat_16, grey, `CENTER 0,+66` | **caption_22, `#C8C8C8`**, move down to `+96` into the ring's open mouth |
| Zap `g->zap` | green→level, `CENTER 0,+108` | **drop**, or fixed terracotta as an inline "↻" left of the countdown |
| Top bar | 24 px icons, batt % below (16, grey) | **~48 px icons**, batt % **caption_22 `#C8C8C8`**; re-tune y to clear ring |

### 7.2 — SERVICE (`build_status`, `ui.c:179-208`)
| Element | Now | Proposed |
|---|---|---|
| Health glyph `st_health` | 30 px, `CENTER 0,-48` | **~80 px**, `~0,-70` — becomes the de-facto title/headline |
| Description `st_val` | font_ext_22, **indicator colour** | font_ext_22, **`#F5F5F5` / `#C8C8C8` (text, NOT indicator colour)** |
| Incident `st_sub` | font_ext_16, **indicator colour** (→ green!) | font_ext_22 for outage/critical; **severity colour, floored amber, never green** (§8.1) |
| (title) | removed (D24) | keep removed; the big glyph + description identify it |

### 7.3 — CLAWD (`splash.c`)
- Anchor the mascot slightly higher (`~y-20`) and add a single **caption line** under it showing the
  worse-of level (e.g. "78%") in the matching data colour — gives the screen one glanceable datum
  without clutter. (Optional; low priority.)

### 7.4 — SETTINGS (`build_settings`, `ui.c:326-368`)
| Element | Now | Proposed |
|---|---|---|
| Title | montserrat_22, grey, `0,-120` | **montserrat_28, terracotta**, `0,-130` |
| "Brightness" | montserrat_16, white, `0,-58` | **font_ext_22 / body**, `0,-70` |
| Slider | default LVGL **blue**, `0,-24` | terracotta handle/indicator, track `#2A2A2A`, `0,-30` |
| "NN%" `bri_val` | montserrat_16, grey, `0,+10` | **caption_22, `#C8C8C8`**, `0,+6` |
| Power button | 200×56, red, label 16, `0,+80` | red `#FF5252`, label_22, move to `~0,+110`; optional FW-version micro-caption at `~+160` |

## 8. Semantic-correctness fixes (must-fix; from the state-correctness review)

These guard *meaning*; the restyle MUST respect them or it will create ambiguous states.

### 8.1 🔴 Incident text rendered green (the #1 inversion)
`ui_update_status()` (`ui.c:588-602`) applies ONE `col = indicator_color()` to **both** `st_val` (line 591)
and `st_sub` (line 598). When `indicator=="none"` → green → the incident detail is green. **Fix:** keep
`st_val` on the indicator colour (it's the headline); colour `st_sub` by **incident severity, floored at
amber whenever `incident_count>0`, never green** — decoupled from the indicator. Plus per D-palette, the
*description text* should really be white/secondary (only the **glyph** carries semantic colour).

### 8.2 Grey is overloaded ("secondary info" == "unknown/stale")
Grey marks both valid-secondary text *and* unknown/stale/unavailable (`indicator_color` fallback `:575`,
fetch-error `:582/:584`, battery-unknown `:528`). A failed fetch then looks like a healthy countdown.
**Fix:** reserve `#C8C8C8` for valid secondary text; give unknown/stale a **dedicated lane** — dim amber,
or grey **+ a non-colour "stale/?" badge**. SERVICE-unavailable text/glyph must not match healthy grey.

### 8.3 Fetch-error & stale are nearly invisible
`ui_update()` early-returns on `!u->ok` → `ui_set_message("fetch error")` writes **only `g5h.sub`**
(`ui.c:471-473` / `418-421`). So 7D's subtitle is never touched (keeps a healthy "resets in 6d 9h"), and
both arcs/%/zap keep stale values+colours — **a failed poll looks like a healthy device.** **Fix:** define
an explicit degraded appearance applied to **both** gauges (e.g. desaturate the arcs + dim the %), so a
data outage is unmistakable. (Adjacent to the 4 dimensions but high-value; P3.)

### 8.4 WiFi-disconnect red = severity inflation
`ui_set_wifi()` (`ui.c:509-516`) paints disconnect **red** — same red as outage / batt-critical. The
`ic_wifi_off` glyph already disambiguates. **Fix:** disconnect → grey/dim-amber; keep red for true danger.

### 8.5 Zap recolours redundantly
`g->zap` mirrors `level_color()` (`:480/:489`) — same colour already on the arc and %. Zero added info,
burns one of three data colours. **Fix:** make it fixed terracotta chrome (or drop it, §7.1).

### 8.6 Guardrails for any size/colour change
1. Three-colour discipline: green=good, amber=caution, red=danger — never reassign.
2. Never paint an incident line green (independent of indicator).
3. "Unknown/stale" must be visually distinct from "good."
4. Keep a **non-colour cue** on every state (shape/word) — colour-blind safety.
5. Don't let semantically-different ambers sit adjacent and read as related.
6. Redundant colour channels are budget, not free (the zap).
7. Glyph *shape* and glyph *colour* must agree on severity (today "unknown" = warn-shape + neutral-grey).
8. Consider splitting `major` vs `critical` glyphs (both `ic_health_err` today).

## 9. Implementation roadmap (phased, actionable)

| # | Phase / change | Dimension(s) | ui.c anchor | Cost |
|---|---|---|---|---|
| **R1** | Grey → `#C8C8C8` everywhere valid-secondary | text-colour | `:96,169,191,200,332,353` | **Cheap** (recompile) |
| **R2** | Countdown + battery % 16 → 22 px | text-size | `g->sub :168`, `batt_lbl :95` | **Cheap** (font compiled) |
| **R3** | Fix green incident → severity-floored `st_sub` | text-colour | `ui_update_status :598` | **Cheap** |
| **R4** | Neutralize/drop zap; WiFi-disconnect not red | icon-colour | `:173,480,489`; `ui_set_wifi :512` | **Cheap** |
| **R5** | Titles → 28 px + terracotta (D27) | text-size/colour | `:156-157,331-332,374` | Moderate (enable font 28) |
| **R6** | Enlarge icons (health 30→80, top-bar 24→48, D28) | icon-size | `gen_icons.mjs`; `add_topbar`, `st_health` | Moderate (regen + re-tune offsets) |
| **R7** | Adopt the §4 palette as named constants | icon/text-colour | `level_color :101`, `indicator_color :568`, literals | **Cheap** |
| **R8** | Redundant status word under % | text-size/colour | `build_gauge`, `ui_update` | Moderate (layout) |
| **R9** | Bigger incident text for outage/critical | text-size | `st_sub` font_ext | **Moderate** (regen `font_ext_28`) |
| **R10** | Fetch-error/stale degraded look on both gauges | text-colour/semantics | `ui_update :471`, `ui_set_stale` | Moderate |
| **R11** | SETTINGS spacing + terracotta slider; CLAWD caption | layout/colour | `build_settings`, `splash.c` | Cheap–Moderate |

**Suggested batches:** **P1 = R1–R4** (cheap, highest impact, recompile-only). **P2 = R5–R8** (the visible
restyle). **P3 = R9–R11** (polish + the semantic-invisibility fixes).
> ✅ **Implemented 2026-06-27 (on-device-confirmed):** R1, R2, R3, R4, R5, R7 + the terracotta slider (R11
> partial). See §13. **Remaining:** R6 (icon size), R8 (status word), R9 (bigger status font), R10
> (fetch-error look), R11 (layout repositioning).

## 10. Feasibility & cost (from the LVGL/embedded pass)

- **Fonts already compiled & free:** Montserrat 12/14/16/22/32/48. **18/20/24/28/36/40 are OFF** — enable
  via `sdkconfig` (`CONFIG_LV_FONT_MONTSERRAT_NN=y`, or menuconfig → Components → LVGL → Font usage).
  Flash cost (raw, `LV_USE_FONT_COMPRESSED` is off): 24 ≈ 11 KB, 28 ≈ 14 KB. **Zero RAM/PSRAM** (fonts are
  `const`→flash). Recompile only.
- **Live status/incident text** (`st_val`/`st_sub`) **must** use `font_ext_*` (UTF-8 punctuation). Bigger =
  **regenerate** via `lv_font_conv` at a larger `--size` (process + glyph range in [02 §10.1](02-DESIGN.md);
  TTF at `C:/esp/SenseCAP-Watcher-Firmware/components/lvgl/scripts/built_in_font/Montserrat-Medium.ttf`),
  add to `main/CMakeLists.txt`, new `LV_FONT_DECLARE`. ≈3–6 KB flash each (small glyph set).
- **Icons:** colour = runtime recolour (trivial constant swap). Size = regen `scratchpad/icontool/gen_icons.mjs`
  at 44–48/80 px; flash scales with px² (~+0.7–1.7 KB/icon; all icons to 48 px ≈ +15–20 KB total). Runtime
  `lv_img_set_zoom(img, 320≈1.25×)` is the zero-flash alternative but softens thin glyphs — **not** chosen
  for the hero icons (D28).
- **Screenshot pipeline & WDT: safe.** None of R1–R11 add/remove a screen, so `ui_screenshot()` `scrs[]`/`names[]`
  (`ui.c:450-451`) and `grab_screens.py`'s `len(frames)<5` cap are unaffected. Larger fonts/icons add trivial
  render time — no WDT risk (only the splash PSRAM canvas is WDT-sensitive, untouched). Flash budget ample
  (app partition ~54 % free; total proposed well under ~100 KB).

## 11. Verification (after implementation)

1. `idf.py -C C:\esp\watcher-claude-usage -p COM18 -b 460800 flash`
2. `<idf-venv-python> tools/grab_screens.py COM18 <out> 130` → 5 PNGs.
3. Read PNGs; compare against [`docs/screenshots/`](../screenshots/) baseline; confirm at arm's length **and**
   step back ~2 m. Check: titles terracotta; secondary text legible; SERVICE incident NOT green; health glyph
   large; icons clear of the ring; no state looks like a different-meaning state (esp. stale vs healthy).
4. Persist the new PNGs to `docs/screenshots/` and update [SPEC.md §3](SPEC.md) + this doc's status to as-built.

## 12. Open decisions / residual (confirm before/at implementation)

- **Secondary-grey value:** `#C8C8C8` (bright, recommended for the across-room requirement) vs a cooler/dimmer
  `#8A8F98` (more elegant up close, lower contrast). Recommended: **bright**, given the both-distances goal.
- **Redundant status word under the %** ("OK/HIGH/FULL"): recommended **yes** — it's the cheapest fix for the
  colour-only level encoding (helps the ~8 % colour-blind population and everyone at distance), at the cost of
  one small label. Confirm wording.
- **5 % brightness floor** (`bri_slider` range starts at 5, `ui.c:343`) is itself a legibility hazard — consider
  raising the minimum to ~15–20 %. Out of the 4 named dimensions but adjacent; confirm if in scope.
- **CLAWD data caption** (§7.3): include or keep the mascot pure?

## 13. As-built — P1 + cheap-P2 (2026-06-27, on-device-confirmed)

**Implemented (all in `ui.c` unless noted), built clean (46 % partition free), flashed COM18, captured via
`grab_screens.py` → [`docs/screenshots/`](../screenshots/):**
- **R7 palette** — added named constants `COL_BRAND/TEXT_PRI/TEXT_SEC/SUCCESS/WARNING/DANGER/MAINT/TRACK`
  (hex per §4) at the top of `ui.c`; refactored `level_color()`, `indicator_color()`, `render_battery()`,
  `ui_set_wifi()`, `ui_set_stale()` and all literals to use them. Amber `#FFC400`, danger `#FF5252` (lighter,
  luminance-separated). Arc/slider track `#2A2A2A`.
- **R5 / D27 titles** — `5H`/`7D`/`SETTINGS` now **terracotta `#D97757` @ `montserrat_28`** (enabled
  `CONFIG_LV_FONT_MONTSERRAT_28=y` in `sdkconfig` + `sdkconfig.defaults`). Title text no longer carries the
  green/blue accent; blue is now used only for `maintenance` (`COL_MAINT`).
- **R1 secondary text** — `PALETTE_GREY #9E9E9E` → `COL_TEXT_SEC #C8C8C8` for countdown, battery %, SERVICE
  text, settings readout, idle wifi/batt icons, modal info.
- **R2 sizes** — countdown `g->sub`, battery `batt_lbl`, settings `bri_val` **16 → 22 px**; "Brightness"
  label + power-button label → 22; countdown nudged y=66→72 for the bigger glyph.
- **R3 / §8.1 incident-green fix** — `st_sub` is now severity-coloured (amber floor, danger for
  major/critical), **decoupled from the indicator**; the SERVICE incident renders **amber**, not green
  (verified on the live "suspended access" incident). Error path: `st_health` err-glyph now `COL_DANGER`
  (shape+colour agree).
- **R4 zap + wifi** — zap is a **fixed terracotta accent** (removed the per-level recolor in `ui_update`);
  wifi-disconnect → amber (`COL_WARNING`), no longer red.
- **R11 (partial)** — brightness slider styled terracotta (indicator + knob) on track `#2A2A2A`; power button
  bg → `COL_DANGER`.

**NOT yet done (P2/P3, deferred — need Node tooling / layout work):** R6 icon enlargement (regen
`gen_icons.mjs` at 44–48/80 px + re-tune `add_topbar` to clear the ring), R9 bigger live status/incident
font (regen `font_ext_28`), R10 fetch-error degraded look on both gauges, R8 status-word under the %, R11
layout repositioning (countdown into the ring mouth, SETTINGS spread, CLAWD caption).

**Verification:** all 5 screens captured post-flash; titles terracotta, secondary text visibly brighter/larger,
SERVICE incident amber, slider/zap terracotta. No build/WDT/screenshot-pipeline regressions (screen set
unchanged → `grab_screens.py` `len(frames)<5` cap still valid).

## 14. As-built — P2 (R6 icons + R9 status font, 2026-06-27, on-device-confirmed)

**Icon tool rebuilt + persisted (the original lived in a temp scratchpad and was lost):**
`tools/icontool/{package.json, gen_icons.mjs}` — `lucide-static` (MIT) SVG → **sharp** raster → alpha-8
C arrays → `main/icons_lucide.c`. Run: `cd tools/icontool && npm install && node gen_icons.mjs`. Per-icon
sizes live in the `ICONS` table.

**R6 — icon enlargement (regen, D28):** top-bar wifi/battery **24→40 px**, all battery tiers 40 px, SERVICE
health glyphs **30→80 px**, zap 24→28 px (small fixed accent). Flash cost ~+50 KB (build 46%→45 % free).
The original `gen_icons.mjs` was gone, so it was reconstructed (the 12 symbol→Lucide-name mappings are in the
new tool). Lucide names: `wifi`, `wifi-off`, `battery[-low|-medium|-full|-charging]`, `circle-check`,
`triangle-alert`, `circle-x`, `wrench`, `zap`.

**R9 — bigger live status font:** generated **`font_ext_28.c`** via `lv_font_conv` (same glyph range as
`font_ext_22`; added to `main/CMakeLists.txt` + `LV_FONT_DECLARE`). SERVICE description `st_val` `font_ext_22`→
**`font_ext_28`** (now wraps to 2 lines — intentional, bigger/legible); incident `st_sub` `font_ext_16`→
**`font_ext_22`**.

**Layout re-tune (forced by the bigger icons):** the enlarged 40 px top-bar icon with the **%-below** stack
(D24) collided with the gauge title (5H/7D) and the 80 px health glyph (SERVICE). Fixed by moving the battery
**% to the RIGHT of the icon** — a horizontal `wifi · batt · NN%` cluster (`add_topbar` + `render_battery`
use `LV_ALIGN_OUT_RIGHT_MID`), icons at `TOP_MID` y=62 (clears the ring), wifi x=-56 / batt x=-2. **This
supersedes D24's below-placement → logged as D29.** SERVICE health re-centered to `0,-56`, `st_val` to `0,+20`,
`st_sub` to `0,+98` to fit the 80 px glyph + 2-line description + incident.

**Verification:** screenshot-confirmed all 5 screens — bigger wifi/battery icons clear the ring with no title
collision; the 80 px health glyph reads as the SERVICE headline; 28 px description + amber incident legible;
no overlaps. Build clean, 45 % partition free, no WDT/pipeline regressions.

**Remaining (P3, optional):** R8 status-word under the % (colour-blind redundancy), R10 fetch-error degraded
look on **both** gauges (today a failed poll still looks healthy — `ui_update` early-returns touching only
`g5h.sub`), R11 countdown into the ring mouth + CLAWD data caption. The 5 % brightness floor (§12) also still
open.

## 15. P2 REVERTED (2026-06-27, D30) — user found it too big

After seeing P2 on-device the user preferred the prior (P1) look: **the enlarged wifi/battery icons and the
bigger SERVICE text were too large.** Reverted P2 back to P1 (kept all P1 colour/semantic wins):
- **Icons** regenerated at original sizes via `tools/icontool/gen_icons.mjs` (`ICONS` table back to top-bar
  **24**, health **30**, zap **24**) → `icons_lucide.c`.
- **SERVICE text** back to `font_ext_22` description / `font_ext_16` incident; `st_health` `0,-48`,
  `st_val` `0,+6`, `st_sub` `0,+78`.
- **Battery %** back **below** the icon (`LV_ALIGN_OUT_BOTTOM_MID`) in a 24 px top cluster (`add_topbar`
  wifi `-24,64` / batt `24,64`) → **D29 reverted, D24 restored**.
- Removed `font_ext_28.c` (from `main/` + `CMakeLists.txt` + the `LV_FONT_DECLARE`). `MONTSERRAT_28` stays
  enabled (P1 titles still use it). `tools/icontool/` kept (now regenerates the 24/30 icons).
- Built clean, flashed COM18, screenshot-verified = matches P1.

**Net live UI = P1.** D28 (moderate icon scale) and R6/R9/D29 are recorded above as tried-and-reverted; the
icon-size knob lives in the `ICONS` table if ever revisited.
