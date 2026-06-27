# 02 — Architecture & Technical Design

> How the system works. The technical reference consulted while building.
> External API details live in [03-API-REFERENCE](03-API-REFERENCE.md); hardware/flashing in
> [04-HARDWARE-AND-FLASHING](04-HARDWARE-AND-FLASHING.md).
> **Last updated:** 2026-06-25 · Parent: [SPEC.md](SPEC.md) · As-built notes in §9; P9 feasibility/design in §12.

## 1. Architecture Overview
**Standalone on-device.** The Watcher's ESP32-S3 does everything: connects to WiFi, calls Anthropic
endpoints over HTTPS with the user's OAuth token, parses the JSON, and renders with LVGL. No host PC.

```
        ┌───────────────────────── SenseCAP Watcher (ESP32-S3) ─────────────────────────┐
        │                                                                               │
 WiFi ──┤  [WiFi/Net] ─► [HTTPS+TLS client] ─► [JSON parser] ─► [State model] ─► [LVGL UI] ──► round screen
        │       ▲                                                     ▲                  │
        │  [Provisioning]                                       [Input: knob/touch]      │
        │       │                                                                        │
        │  [Secure store: NVS + AES-256-GCM, PIN-derived key] ◄── token                  │
        └───────────────────────────────────────────────────────────────────────────────┘
```

## 2. Data Flow
1. **Boot** → init display/LVGL, load config (WiFi creds, encrypted token) from NVS.
2. If unconfigured → enter **provisioning** (captive portal / on-device entry) for WiFi + token + PIN.
3. **Connect WiFi**; on success start the poll loop.
4. **Poll loop** (every 60 s): HTTPS `POST /v1/messages` (max_tokens:1 probe) with auth headers — the
   D11 fallback. (`status.claude.com` GET for incidents = P4, not yet wired.)
5. **Parse** the `anthropic-ratelimit-unified-*` **response headers** → **State model** (5h/7d util% +
   reset epoch + status). (`/api/oauth/usage` JSON path unused — setup-token lacks `user:profile`; D11.)
6. **Render**: LVGL view layer redraws from state; show stale/error states on failure.
7. Sleep until next interval (respect 429 backoff).

> Exact endpoints, headers, and response schemas: [03-API-REFERENCE](03-API-REFERENCE.md).

## 3. Component Breakdown
| Component | Responsibility |
|-----------|----------------|
| **Provisioning** | First-run WiFi + token + PIN capture (captive portal AP or on-device UI). |
| **Secure store** | NVS-backed; AES-256-GCM encrypt/decrypt token; PIN-derived key (KDF). |
| **Net / HTTPS client** | WiFi mgmt, TLS (cert handling), GET requests, retry/backoff. |
| **Data-source clients** | One per endpoint (oauth/usage, status, optional messages-header fallback). |
| **State model** | Canonical in-memory snapshot of usage + status + freshness/last-error. |
| **UI / view (LVGL)** | Screens, widgets, animations; redraw on state change. |
| **Input** | Knob (rotate/press) + touch (CHSC6x) → screen navigation. |
| **Power** | Brightness control, battery read, always-on vs sleep behavior. |

## 4. UI / Screen Design
- **Canvas:** round **412×412**, 1.45"; design for a circle (corners cut). Assume RGB565 until confirmed.
- **Framework:** LVGL (+ `esp_lvgl_port`); `rlottie` available for vector animation if desired.
- **Screen inventory (draft):**
  1. **5-hour** — large arc/ring showing util %, center label, reset countdown.
  2. **7-day** — same treatment for the weekly window.
  3. **Per-model** — Opus / Sonnet weekly buckets — ⏸️ deferred (D11; absent from `/v1/messages` headers).
  4. **Status** — Claude service health; incident text if any.
  5. **Setup / info** — WiFi + token status, device info.
- **Navigation:** knob rotate = cycle screens; press = refresh / enter; touch optional.
- **States per screen:** loading · fresh · stale (last-known + age) · error/offline.

## 5. Security Design (modeled on claude-usage-stick)
- Token captured during provisioning, **never stored in plaintext** and **never compiled into the firmware**.
- **TLS**: validate Anthropic certs against the bundled CA roots (`esp_crt_bundle`).
- Required request headers (incl. `anthropic-beta`, `User-Agent`) per `03-API-REFERENCE`.

### 5.1 As-built (P6 / T13, 2026-06-24) — boot-and-go, no PIN
- **User decision:** this is a headless ambient device, so a PIN-at-every-boot was rejected in favour of
  **boot-and-go software obfuscation** (auto-decrypt on boot, no prompt). The original PIN-KDF design is
  therefore **not** implemented (no PIN, no lockout/wipe).
- **`secure_store.c`** — AES-256-GCM (mbedTLS). Key = `SHA-256(MAC(6) ‖ random salt(16) ‖ compiled pepper)`.
  Self-contained blob `[salt:16 ‖ nonce:12 ‖ tag:16 ‖ ciphertext]`. Fresh salt+nonce per save.
- **`config.c`** — NVS namespace `wcfg`: `ssid`/`ver`/`poll` plaintext; `wifi_pass`+`token` packed into one
  fixed-size struct and stored as the GCM blob (`sec`). `prov`=1 marks provisioned. `config_factory_reset()`
  erases the namespace.
- **THREAT MODEL (documented in `secure_store.h`):** defeats casual flash-string extraction (`strings` over a
  dump) and gives integrity (GCM tag). Does **NOT** defeat a determined attacker who dumps *this* chip's flash
  and reads the firmware (MAC is readable; pepper is in the image). AC6 ("unreadable from a flash dump") is thus
  **partially** met — full at-rest protection needs ESP32 HW flash-encryption (irreversible eFuse burn,
  deliberately not enabled to preserve the restore path). Accepted tradeoff for a personal ambient device.

### 5.2 As-built (P6 / T6, 2026-06-24) — captive-portal provisioning
- **`provisioning.c`** — first boot (or after factory reset) brings up an **open SoftAP `Watcher-Setup`**, a
  minimal **captive UDP-DNS** responder (answers every A query with 192.168.4.1 so the phone pops the portal),
  and **`esp_http_server`** serving a styled form (WiFi SSID/password, OAuth token, `claude --version`, poll
  interval) with a wildcard catch-all GET. `POST /save` url-decodes the fields, fills `config_t`, the caller
  `config_save()`s and `esp_restart()`s. `ui_show_setup()` shows on-screen join instructions.
- **Re-provision triggers:** hold the knob **~3 s** during normal use (LVGL encoder `LV_EVENT_LONG_PRESSED`,
  threshold raised to 3000 ms to avoid accidents) **or** hold the knob **at boot** — both call
  `config_factory_reset()` then reboot into the portal. (Knob polarity verified: released reads high.)
- **`main.c` flow:** load config → if unprovisioned run the portal (reboot on save) → else `wifi_sta_connect`
  with stored creds (retry-forever on outage, no wipe), `claude_usage_set_auth(token,version)`, SNTP, poll at
  `cfg.poll_s`.

## 6. Error Handling & Edge Cases
- **WiFi down / no IP** → offline state, auto-retry; keep last-known on screen.
- **HTTP 429** → exponential backoff; surface "rate-limited" briefly; lengthen interval.
- **HTTP 401/403 / expired token** → prompt re-auth; do not spin.
- **Malformed/partial JSON** → keep prior state, count errors, show stale.
- **Reset-time parsing** → handle ISO-8601 `resets_at`; render countdown from device clock (NTP sync).
- **First boot / no config** → provisioning, not a crash.

## 7. Config & Persistence (NVS)
- Encrypted OAuth token (+ GCM nonce/tag), WiFi SSID/pass, PIN verifier, poll interval, brightness,
  last-known snapshot (optional, for instant boot display).

## 8. Open Design Questions
- ✅ Arc vs bar → **arcs chosen** (270° gauge + center %); implemented.
- Whether to mirror the stick's optional model-status "mascots" or keep status minimal.
- OAuth token refresh/expiry strategy (see [SPEC.md](SPEC.md) §5).

## 9. As-Built Notes (2026-06-24)
- **Project:** `C:\esp\watcher-claude-usage` (ESP-IDF, target esp32s3), separate from the cloned reference
  repo; pulls the `sensecap-watcher` BSP via `main/idf_component.yml` `override_path`. `main` declares
  **no explicit REQUIRES** (inherits all components, incl. the BSP's transitive headers).
- **Data:** `claude_usage.c` runs the `POST /v1/messages` probe and reads rate-limit headers into a
  `usage_t {pct_5h, pct_7d, status, reset_5h, reset_7d}`. **`claude_status.c`** (P4) does an unauthenticated
  GET of `status.claude.com/api/v2/summary.json` into a 6 KB PSRAM buffer, parses it with **cJSON** into
  `service_status_t {indicator, description, incident_count, incident[]}`. `http_get.c` = generic GET helper
  (reused by `claude_status.c`). WiFi via raw `esp_wifi` (`wifi_connect.c`). Time via `esp_netif_sntp`.
- **UI:** `ui.c` — 3 LVGL screens (5H / 7D / SERVICE): 270° arcs + center % (green/orange/red by level),
  "resets in Xh Ym" from SNTP. Knob = ENCODER indev added to an `lv_group`; rotate cycles screens (load
  on `LV_EVENT_FOCUSED`). RGB LED **off in steady operation** (the per-poll level cue was removed to save battery — D31; brief boot/setup magenta + connecting blue kept). 60 s poll loop in `main.c` (fetches usage **and**
  service health each cycle). **SERVICE screen (P4):** shows `description` colored by `indicator`
  (none→green, minor→orange, major/critical→red, maintenance→blue, unknown→grey) + first incident name
  (`+N more` if several) or "no incidents". **`ascii_sanitize()`** transliterates UTF-8 punctuation in
  incident/description text to ASCII (BSP fonts are ASCII-only) — interim until FR11 (§10.1).
- **Critical config:** `CONFIG_LVGL_PORT_TASK_AFFINITY_CPU1=y` (LVGL flush must not share CPU0 with WiFi,
  or SPI "queue color failed" → frozen screen), `FREERTOS_HZ=1000`, `LV_COLOR_16_SWAP=y`, `LV_MEM_CUSTOM=y`,
  `ESP_MAIN_TASK_STACK_SIZE=8192` (P6 call chain), `HTTPD_MAX_REQ_HDR_LEN=2048` (P6 phone headers).
- **As-built (P7, 2026-06-24) — power/polish:** `main.c` poll loop tracks last-good time (`esp_timer`); on a
  failed fetch it **keeps the last-known arcs/%** and shows **"stale Xm"** (orange) on the gauges (`ui_set_stale`),
  turns the WiFi icon red when `wifi_is_connected()` is false (the STA handler auto-reconnects forever in the
  background), and keeps the last-known service health. **429 backoff:** `usage_t.http_status` drives an
  exponential interval (×2 up to 600 s, reset on success). **Battery (real):** `bsp_battery_is_present` /
  `bsp_battery_get_percent` / `bsp_system_is_charging` each poll → `ui_set_battery` (charging bolt on USB,
  percent icon on a pack; ADC self-inits, rail powered by `bsp_io_expander_init`). **Brightness:** configurable
  static `cfg.brightness` (portal field, default 80 %) → `bsp_lcd_brightness_set` at boot. **Poll interval:**
  `cfg.poll_s` from the portal. (Auto-dim/sleep on inactivity deliberately deferred — backlog.)
- **Secrets (as-built P6):** WiFi creds + OAuth token are captured at runtime via the captive portal (§5.2) and
  stored AES-256-GCM-encrypted in NVS (§5.1) — **no secrets compiled in**. `main/secrets.h` remains git-ignored
  but is now referenced only under the disabled `P6_SEED_FROM_SECRETS` guard (a one-shot test seed). Boot-task
  stack raised to 8 KB (`CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192`) for the provisioning/screenshot call chain.
  Flash: COM18 @ 460800; display/touch = SPD2010.

## 10. Stretch Visual Features — Design (P8: FR11 / FR12 / FR13)
> Adapted from the **Clawdmeter** reference (`references/Clawdmeter/README.md` + `CLAUDE.md`).
> **Key divergence:** Clawdmeter runs **LVGL 9 + Arduino_GFX** on a square/portrait AMOLED; we run
> **LVGL 8.4.0 + esp_lvgl_port** on the **round 412×412 SPD2010 (RGB565, `LV_COLOR_16_SWAP=y`)**. The most
> important consequence: `lv_font_conv` emits **LVGL-8 format**, so the four "LVGL 9 patches" Clawdmeter's
> README insists on (remove `#if … >= 8` guards, drop `.cache`, add `.release_glyph/.kerning/.static_bitmap/
> .fallback/.user_data`) are **NOT needed here** — generated fonts/images drop in directly. Flash headroom is
> ample (app partition 3 MB, ~54 % free after P4).
> All three are **independent** and can be sequenced per-phase (no fixed order — user decision 2026-06-24).

### 10.1 Extended-glyph font (FR11) — retires the `ascii_sanitize` shim
- **Why:** BSP fonts are `lv_font_montserrat_*` (ASCII-only), so curly punctuation in live incident text
  renders as boxes; today `ascii_sanitize()` transliterates it. FR11 renders the real glyphs instead.
- **Scope (user decision):** **punctuation only, no CJK** → font stays small; **no** `LV_FONT_FMT_TXT_LARGE`.
- **Build:** `npm i -g lv_font_conv`, then per size we use (16 / 22 / 32; 48 for the big % numerals):
  ```bash
  lv_font_conv --font Montserrat-Medium.ttf --size 22 --bpp 4 --no-compress \
    --format lvgl --lv-include "lvgl.h" \
    -r 0x20-0x7E,0xB7,0x2013-0x2014,0x2018-0x2019,0x201C-0x201D,0x2026 \
    -o main/font_ext_22.c
  ```
- **Integrate:** add `font_ext_*.c` to `main/CMakeLists.txt` SRCS, `LV_FONT_DECLARE(...)`, point the SERVICE
  labels (and any text that shows API strings) at the extended font; then delete `ascii_sanitize()` (or keep
  it as a defensive net for genuinely unmapped codepoints). No sdkconfig flag needed for this small range.
- **Verify:** SERVICE incident line shows a real "'" / "—" / "…" with no box.

### 10.2 Lucide icons (FR12)
- **Why:** replace text-only cues (WiFi/health/battery) with crisp glyphs.
- **Format:** Lucide PNGs are **black-on-transparent** → must **tint white** or they vanish on the dark UI.
  Two LVGL image formats: **baked RGB565** (over an opaque/known background — cheapest) vs **RGB565A8**
  (planar: `w*h` RGB565 then `w*h` alpha; `data_size=w*h*3`) for overlaying non-uniform backgrounds (e.g. an
  icon over the arcs). Honor `LV_COLOR_16_SWAP` in the converter's byte order.
- **Tooling:** port Clawdmeter's `tools/png_to_lvgl.js` (`<in.png> <symbol> [--tint=RRGGBB|--no-tint]`) →
  emit `lv_img_dsc_t` C arrays into `main/icons.h`; render with `lv_img_create` / `lv_img_set_src`.
- **Candidates:** WiFi connected/offline, service-health dot, battery states (dovetails with P7 battery).
- **As-built (T17, 2026-06-24):** chose **`LV_IMG_CF_ALPHA_8BIT`** (alpha-only, half the size of RGB565A8) so a single bitmap recolors at runtime via `lv_obj_set_style_img_recolor` — no per-state regen. Pipeline = `lucide-static` SVG → **sharp** raster (density 512) → alpha plane → C array (`scratchpad/icontool/gen_icons.mjs` → `main/icons_lucide.c/.h`, 12 icons). Helper `new_icon()`/`set_icon()` in `ui.c`. Icons: WiFi+battery top-bar on every screen (`ui_set_wifi`/`ui_set_battery`), health glyph on SERVICE (circle-check/triangle-alert/circle-x/wrench by indicator), zap on gauges (recolor by level). **Battery has no data source** (no BSP API) → grey "unknown" placeholder until P7.
- **✅ Resolved (T17b, 2026-06-24):** moved the top-bar icons from `LV_ALIGN_TOP_MID, ±24, 22` → `±24, 64`, dropping them into the open hub below the ring's inner edge (ring top arc ≈ y26–54). Clear of the arc on both gauges, above titles on all screens. The separately-reported "pale WiFi on SERVICE" was a perception artifact — pixel sampling confirmed identical green on all 3 screens; no recolor defect.

### 10.3 Splash / ambient animation (FR13) — "what I really want on the device"
- **Concept:** a dedicated animation screen (boot splash and/or a 4th knob screen) whose creature/animation
  gets **busier as usage climbs** — Clawdmeter buckets by *rate-of-change* of session %; we poll every 60 s,
  so the simpler driver is the **absolute level** `max(pct_5h, pct_7d)` → mood group (calm / active / frantic).
- **Engine:** pixel-art frames as RGB565 C arrays (per-animation **10-color palette**, cells index 0–9, à la
  Clawdmeter's 20×20 sprites); an `lv_timer` advances frames; on the round panel, center the sprite inside the
  safe circle and scale `CELL = min(W,H)/grid`. Auto-rotate among a group's animations every ~20 s.
- **Alternative:** the BSP already vendors **rlottie** (a submodule) for vector animation — heavier but
  resolution-independent; pixel-art C arrays are lighter and match the reference. Decide at implementation.
- **Asset source (DEFERRED — user decision):** spec the engine + data format now; choose assets later.
  Option A = **claudepix** Clawd sprites (Clawdmeter's `tools/scrape_claudepix.js` + `convert_to_c.js`) —
  authentic but **Anthropic-copyrighted (licensing gray area; OK for a personal device)**. Option B =
  original / CC-licensed pixel art (user supplies/approves — user prefers not to have the assistant author art).
- **Integration:** new `splash.c` + generated `splash_animations.h`; add the screen to the `lv_group`; pick the
  mood group from the latest `usage_t`. Frames live in PSRAM.
- **As-built (T18, 2026-06-24):** shipped as the **4th knob screen "CLAWD"** (not a boot splash) + **4-level** mood
  (idle<25 / normal<50 / active<80 / heavy≥80, driver = `max(pct_5h,pct_7d)`), both per user decision. Engine
  `main/splash.c/.h` ported from Clawdmeter `splash.cpp` to LVGL 8.4 + ESP-IDF: 400×400 (`CELL=20`) `lv_canvas`
  (`LV_IMG_CF_TRUE_COLOR`) in PSRAM, centered (round corners clipped, all black); an **`lv_timer`** (~30 Hz, runs
  in the LVGL task ⇒ lock-safe) advances frames by each frame's `hold` and round-robins ~20 s within the current
  group, **only while CLAWD is the active screen** (`lv_scr_act()` guard) to save CPU. Groups resolved by anim
  name à la the reference. **Data:** reused the reference `convert_to_c.js` on the already-vendored
  `references/Clawdmeter/tools/claudepix_data/*.json` (13 anims) → `main/splash_animations.h` (177 KB) — **no
  network scrape**. Palettes left as standard RGB565 and converted via **`lv_color_make` at runtime** ⇒
  byte-order-agnostic under `LV_COLOR_16_SWAP` (cleaner than emitting pre-swapped `uint16`); brand remap
  `#cd7f6a→#d97757` retained. `ui.c` adds the screen to the encoder `lv_group` after SERVICE with the same
  focus-driven `lv_scr_load_anim` nav, and calls `splash_set_level()` from `ui_update`. Verified via
  `grab_screens.py` (after bumping its frame-count cap 3→4) — creature renders in brand terracotta. rlottie
  alternative not used (pixel-art is lighter and matched the reference).

## 11. UI Verification Tooling (T19 — devex, to evaluate)
> **Problem:** every UI iteration currently needs the user to manually look at the device and report back
> (slow loop; it's how the T17b icon-overlap issue surfaced). Goal: automate visual feedback.

**Option A — `references/Lvgl-mcp-esp32` (headless LVGL simulator as an MCP server).** Compiles C snippets/files
with MSVC + CMake/Ninja, renders a **PNG + JSON widget tree** (x/y/w/h + computed styles), exposes
`lvgl_render` / `lvgl_render_full` / `lvgl_inspect` / `lvgl_set_resolution`. Feasibility checked 2026-06-24:
- ✅ MSVC present (VS2022 Community `cl.exe`); CMake/Ninja come with ESP-IDF; Node ≥18 present.
- ⚠️ **LVGL 9.2** vs our **8.4** — different API (`lv_button_create`/`lv_screen_active`/color+image structs); our
  real `ui.c` + generated 8.4 fonts/icons won't compile there unmodified.
- ⚠️ Renders a **square 32-bpp XRGB8888** framebuffer; our panel is **round 412×412 RGB565 (`LV_COLOR_16_SWAP`)**.
  No round-corner mask, different fonts (sim has Montserrat 12–24 only) → layout/clipping won't match the device,
  i.e. it can't reliably catch exactly the round-edge/arc-overlap class of bug.
- **Verdict:** useful as a fast *off-device layout sandbox* for greenfield v9 sketches; **not** a faithful
  reproduction of our device. The **widget-tree JSON** (programmatic x/y/w/h overlap checks) is its best part.

**Option B — on-device framebuffer screenshot over serial (Clawdmeter-style).** Add a `screenshot` serial command
that captures the LVGL draw buffer (LVGL 8.4 ships `src/extra/others/snapshot/lv_snapshot.h`; enable
`LV_USE_SNAPSHOT`) → encode (e.g. raw/PNG) → stream over UART → host saves a PNG. **Ground-truth**: real round
panel, real RGB565/byte-swap, real fonts/icons, real data — exactly what the user sees.
- Cost: firmware command + a host-side capture script; bandwidth (412×412×2 ≈ 340 KB raw per frame over UART).

**Recommendation (to confirm next session):** Option B for *verifying this device's actual UI*; Option A optionally
as a quick layout previewer if we accept the 9.2/round/RGB565 caveats. Could also do both.

### 11.1 As-built — Option B implemented (T19, 2026-06-24)
**Chose Option B** (ground-truth, no MCP). `LV_USE_SNAPSHOT` was already `y`. Files:
- **`main/screenshot.c`** — `screenshot_dump_all(scrs, names, n)`: under `lvgl_port_lock`, `lv_snapshot_take_to_buf`
  each screen (`LV_IMG_CF_TRUE_COLOR` → RGB565) into a PSRAM buffer; **release the lock**, mute logs
  (`esp_log_level_set("*", ESP_LOG_NONE)`), then stream each as **RLE-hex** (`@@SHOT name w h` … `@@END`,
  per run = 4 hex count + 4 hex raw pixel). Capture-under-lock / stream-unlocked keeps the LVGL task fed (no WDT).
- **`ui_screenshot()`** in `ui.c` passes the 3 screens; **`main.c`** calls it once per boot after first data.
- **`tools/grab_screens.py`** — ESP-IDF-venv python (pyserial + stdlib `zlib`, no PIL, no MCP): pulse RTS to
  reset, read frames, un-swap RGB565 (`LV_COLOR_16_SWAP`), write `shot_<name>.png`. The assistant `Read`s them.
- **Workflow:** `idf.py -p COM18 flash` → `python tools/grab_screens.py COM18 <outdir> 90` → Read PNGs.
- **Proven 2026-06-24:** 3 screens @412×412 captured; surfaced the T17b arc-overlap automatically.
- **Caveats:** ~115200 baud → a few KB of RLE per screen (fast because the UI is mostly black); on-boot dump only
  (host-triggered RX = future iter); snapshots the screen objects directly (no need to be the active screen).

## 12. Settings & Power UX — Feasibility & Design (P9)
> **Status:** feasibility study complete (2026-06-25), evidence-backed + adversarially verified across our app,
> the `sensecap-watcher` BSP, the stock `factory_firmware`, the vendored LVGL 8.4, and the OSHW schematic.
> **Verdict: all three feasible.** Decisions locked: shutdown = **true power-off** (`bsp_system_shutdown`);
> Settings = a **5th knob screen** with press-to-edit brightness + a Power-off item with confirm.
> This section is the design; implementation is P9 (T20–T23 in [05](05-IMPLEMENTATION-PLAN.md)).

### 12.1 F1 — Charging-indicator fix (defect) — *trivial*
- **Bug:** the battery icon shows "charging" with USB **unplugged**. Root cause: `bsp_system_is_charging()`
  (`sensecap-watcher.c:357`) reads **the wrong pin and inverts it** — `return !(get_level(BSP_PWR_CHRG_DET)==0)`,
  i.e. "charging" when `CHRG_DET` is HIGH. All board detect pins are **active-low**; `CHRG_DET` (open-drain)
  floats HIGH when not charging/unplugged, so the icon reads charging exactly when it shouldn't. The sibling
  `bsp_system_is_standby`/`bsp_battery_is_present` correctly use `==0`.
- **Correct signal:** the stock firmware never touches `CHRG_DET` — it derives USB/charge state **only** from
  `BSP_PWR_VBUS_IN_DET` active-low (`view.c:943`, `app_device_info.c:1382/1429`): `on_usb = (level(BSP_PWR_VBUS_IN_DET)==0)`.
- **Fix (Option A, recommended):** in `main.c` (the two `ui_set_battery(...)` calls) replace the
  `bsp_system_is_charging()` argument with `(bsp_exp_io_get_level(BSP_PWR_VBUS_IN_DET)==0)` (hoist a `bool on_usb`).
  `ui.c` needs **no change** (`ui_set_battery` already renders the charging icon from its boolean). No BSP edit.
  *(Optional strict "actively charging": `on_usb && level(CHRG_DET)==0`; a "full" state: `on_usb && CHRG_DET!=0 && STDBY_DET==0` — only read CHRG/STDBY while `on_usb`. Needs the charger-IC truth table to be sure.)*
- **✅ As-built (T20, 2026-06-25; D19 + D22) — went beyond the trivial fix after on-device feedback.** The initial Option-A fix worked on unplug but exposed two more issues: (a) **re-plug didn't refresh** — the battery/USB UI was only updated inside `main.c`'s 60–600 s (backoff-stretched) poll loop; (b) the **unplugged glyph was an empty grey outline** — `bsp_battery_is_present()` (gating in `main.c`) read not-present, falling to `ic_batt` grey; and the user wanted a numeric **%**. **Rework:** the badge is now **self-driven** by `power_timer_cb` (an `lv_timer` at **1 s**) in `ui.c` — reads `BSP_PWR_VBUS_IN_DET` every tick (responsive plug/unplug **both ways**), `bsp_battery_get_percent()` every **30 s** + immediately on a USB transition (ADC is log-spammy; this mirrors stock `app_device_info.c:1382/1428`). **Dropped the `is_present` gating** (stock doesn't use it; the device is on battery whenever USB is absent). Added a numeric **`NN%`** `lv_label` under the battery icon (`font_ext_16`, recolored: green charging / red<15 / orange<50 / green). Removed the battery block from the `main.c` poll loop and the `ui_set_battery` prototype from `ui.h`. Screenshot-verified (100% + bolt, no SERVICE-title collision); user-confirmed live unplug/re-plug.

### 12.2 F2 — True power-off shutdown — *small* (already shipped in factory FW; we expose it)
- **The cut:** `bsp_system_shutdown()` (`sensecap-watcher.c:350`) drives `BSP_PWR_SYSTEM`=0 (PCA9535 P1.2 =
  schematic **EXP_POWER_ON/OFF** → Q10 gate → buck `EN`). Genuine power-off (latch/quiescent draw); writes no
  persistent flag.
- **Wake back on (schematic-verified, fully recoverable, no reflash):** (1) **press-and-hold the wheel button
  ~3 s** — the button shares the rotary net and momentarily pulls `EN` above threshold; the ESP boots and
  `bsp_io_expander_init()` re-latches `BSP_PWR_SYSTEM=1` (`:169`) so the user can release; (2) **plug in USB-C**
  (auto-on); (3) RST pin-hole. Dead-battery caveat: a too-low pack won't long-press-boot → connect USB first.
- **Mandatory UX guards (from the shipped factory path):**
  1. **Confirm gesture** before shutoff (user chose a confirm dialog; **default-focus Cancel**).
  2. **Gate on power source:** if `BSP_PWR_VBUS_IN_DET==0` (USB present) a true power-off is a **silent no-op**
     (rail held by USB) — relabel the action **"Reboot"** and `esp_restart()` instead (factory does exactly this:
     `main.c:106-108`, `view.c:288-311`).
  3. **Clean shutdown:** disconnect/flush + unmount SD/SPIFFS before cutting power (factory `main.c:88-106`) to
     avoid FS corruption. (Our app currently mounts neither, but keep the pattern if that changes.)
  4. **Tell the user how to wake:** show "Hold the wheel button ~3 s to power on (or connect USB-C)" — otherwise
     the device looks bricked.
- **Alternative considered:** `bsp_system_deep_sleep(0)` (ext0 wake on the IO-expander INT = knob/touch) — instant
  wake, rail kept alive, higher idle drain. Not chosen (user wants true off). *Correction to one finding: the BSP
  shutdown helper DOES exist — use `bsp_system_shutdown()`, not `esp_deep_sleep_start`/backlight-off.*
- **✅ As-built (T23, 2026-06-25; D23) — on-device-confirmed (reboot-on-USB path tested).** Power `lv_btn` on the
  SETTINGS screen → `open_power_confirm()` builds a modal on **`lv_layer_top()`** (covers all screens) with a
  message + **Cancel**/confirm buttons in a **temporary `lv_group`** the encoder is switched to (Cancel
  default-focused via `lv_group_focus_obj`); `modal_close()` restores the encoder to the main group and deletes
  the overlay. Confirm re-reads `BSP_PWR_VBUS_IN_DET`: **USB present → `esp_restart()`** (the button is
  live-relabelled **"Reboot"** by `power_timer_cb`, and the dialog copy adapts); **on battery → `bsp_system_shutdown()`**
  with on-screen "hold the wheel ~3 s to power on (or USB)". Chosen over `lv_msgbox` (cleaner encoder focus control).
- **✅ DEF2 — wake-gesture / reprovision-gesture collision (found 2026-06-25; FIXED T24/D25, on-device-confirmed 2026-06-25).** The hold-wheel-to-wake gesture conflicts with the **boot-time** reprovision gesture: `app_main()` calls `knob_held_at_boot()` (samples `BSP_KNOB_BTN` ~50 ms into boot) and, if held, runs `config_factory_reset()` + enters provisioning (P6/D17). Because waking holds the wheel (shared rotary net) for ~3 s *through* the boot, the device sees the knob held → **factory-resets itself → re-asks WiFi login on every wake.** NVS creds are intact across the power cut; they're erased by accident. **Fix (D25, T24 — ✅ done & on-device-confirmed): deleted the `knob_held_at_boot()` trigger** (and its `app_main()` call) from `main.c` so a provisioned device always boots to normal operation. Reprovision still available via the in-use 3 s knob long-press (`reprovision_cb`), plus re-flash / `nvs erase`. (Considered but rejected: distinguishing wake-hold vs reprovision-hold by timing — fragile; moving reprovision to a Settings item — more work, deferred as a possible later enhancement.)

### 12.3 F3 — Settings screen (brightness + shutdown) — *medium*
- **Encoder nav model (the load-bearing detail):** LVGL 8.4 distinguishes **editable** (`lv_slider`/`lv_arc`)
  vs **non-editable** (`lv_obj`/screens) focused objects. Recommended: **one group**, add the SETTINGS screen +
  the brightness `lv_slider` (+ a power `lv_btn`) as members after the four screens, in deliberate order:
  `5H, 7D, SERVICE, CLAWD, SETTINGS, BRIGHTNESS-slider, POWER-btn`. Rotate cycles members; pressing the slider
  enters **edit mode** → rotate adjusts 5–100 → press exits (`lv_indev.c` press-to-edit). Non-editable screens
  keep cycling exactly as today.
- **Important nuance (adversarial verdict = "uncertain", clarified):** LVGL's `editing` flag is **group-wide,
  not per-object** — you cannot literally "edit the slider *and* cycle screens at the same time." Once in edit
  mode, rotation drives the slider until you press to exit; the slider is a peer in one nav ring. This **is** the
  intended UX and is **non-sticky given ≥2 group members** (the edit-exit long-press toggle is gated by
  `obj_count>1`, `lv_indev.c:629` — a lone-slider group would get permanently stuck, so never isolate it alone).
  A dedicated sub-group (swap on enter/exit) is possible but adds stuck-state risk → **not recommended**.
- **Brightness:** live preview via `bsp_lcd_brightness_set(value)` on `LV_EVENT_VALUE_CHANGED` (precedent
  `main.c:120`). **Persistence:** do **NOT** call `config_save` per detent — it re-encrypts the whole AES-GCM
  secret blob (`config.c:65-94`). Add a lightweight `config_set_brightness(int)` that writes only NVS `bri`
  (no `sec` touch), called on edit-exit/debounce.
- **Power-off item:** a non-editable `lv_btn` → confirm dialog (Cancel default-focused) → the F2 path.
- **Integration:** new `build_settings()` in `ui.c`; add to `grp` (`ui.c:229`); bump `ui_screenshot` arrays 4→5
  (+`"SETTINGS"`; `screenshot_dump_all` already allows `MAX_SCR=8`). `NSCR=3` (top-bar arrays) can stay — Settings
  (like CLAWD) needs no wifi/batt badge unless we bump `NSCR` + the `ui_set_wifi/battery` loops.
- **MUST-FIX:** `reprovision_cb` is on `LV_EVENT_LONG_PRESSED` (`ui.c:111/224`, `long_press_time=3000`). On the
  **editable** slider a long-press toggles edit and emits **no** `LONG_PRESSED` → the 3 s factory-reset gesture is
  silently dead while the slider is focused. Decide: document "reprovision only from non-editable screens", or move
  the gesture to a non-editable element. Also add edit-mode auto-exit on Settings blur/inactivity (anti-stuck).
- **✅ As-built (T21+T22, 2026-06-25; D21/D23) — on-device-confirmed.** `build_settings()` adds the 5th screen
  `set_scr` (title + **brightness `lv_slider` 5–100** + **power `lv_btn`**) and appends `set_scr, bri_slider,
  pwr_btn` to the single encoder group after CLAWD (nav ring as designed). Slider is editable (press-to-edit,
  rotate, press-exit); **`VALUE_CHANGED` → live `bsp_lcd_brightness_set`**; **`DEFOCUSED` → `config_set_brightness()`**
  which writes **NVS `bri` only** (no token re-encrypt). Initial value passed in via the new `ui_init(int brightness)`
  signature (from `cfg.brightness`). A `settings_member_focus_cb` on the slider/button **loads `set_scr` when either
  is focused** (fixes the wrap-from-5H case where a non-screen member would otherwise show on a hidden screen).
  **MUST-FIX resolution:** kept `reprovision_cb` on the 5 screens (works everywhere except while the slider itself is
  focused — documented, not relocated); a dedicated edit-auto-exit timer was unnecessary (≥2 group members ⇒
  non-sticky). **Battery % sits below the icon; the "SERVICE" screen title was removed** (D24) so the top bar has clear space (the health glyph + "All Systems Operational" identify the screen). *(An earlier D23 attempt placed the % beside the icon; reverted per user preference.)*

### 12.4 Residual risks / confirm on-device
- Charger-IC truth table (CHRG vs STDBY) unconfirmed — only needed for a strict "charging" / "full" icon beyond
  "on USB power". True-off quiescent current + exact ~3 s hold RC unmeasured (matter only for shelf-life/UX copy).
- Verify on a real unit: unplug→icon clears (F1); USB-present "Power off"→relabels "Reboot" no-op (F2);
  wheel-3 s power-on after a true shutdown (F2); brightness edit doesn't strand the encoder, reprovision decision
  (F3).

## 13. Audio — Spoken Alerts (P11, as-built 2026-06-27, D32)
> The Watcher has an **ES8311 codec + speaker** (04 Audio row). FR18 speaks 5H-usage and
> battery threshold crossings aloud, **fully offline** (matches the no-host design, D2).

### 13.1 Clips (offline TTS)
- 10 fixed phrases, one per threshold. Generated on the PC with **Windows SAPI** (`System.Speech`,
  voice "Microsoft Zira", 16 kHz/16-bit mono WAV) → `tools/audio/gen_clips.ps1`. **No cloud, no
  on-device TTS** (ESP-IDF English TTS isn't viable; cloud-TTS would break standalone, D2).
- `tools/audio/wav_to_c.py` trims silence, **peak-normalizes + tanh soft-limits** each clip for loudness
  (SAPI leaves ~10 dB headroom), emits `main/speak_clips.c` (10 `int16` PCM arrays + `SPEAK_CLIPS[]`) +
  `speak_clips.h` (enum `SPK_*`, `SPEAK_SAMPLE_RATE 16000`). ~731 KB PCM.
- Regenerate: `pwsh tools/audio/gen_clips.ps1 [voice]` then `python tools/audio/wav_to_c.py`.

### 13.2 Playback (`main/speak.c`)
- `speak_init()` → `bsp_codec_init()` (BSP default 16 kHz/16/mono **matches the clips — do NOT call
  `bsp_codec_set_fs` again**: the redundant reopen left the play channel disabled and `esp_codec_dev_write`
  spun → Task-WDT) + `bsp_codec_volume_set(95)` (BSP cap) + `bsp_codec_mute_set(false)`. Spawns a `speak`
  task fed by a queue.
- The task plays a clip in **small chunks** (`SPEAK_CHUNK 1024` samples via `bsp_i2s_write`) — a single
  ~100 KB write spun and tripped the Task-WDT; chunking yields between writes (per the BSP `openai-realtime`
  example). Playback runs off the LVGL/poll threads, so callers never block.

### 13.3 Threshold logic
- **5H usage** announces when it **rises** into a band: 25/50/75/90/100 → `SPK_USAGE_*`. Called from the
  `main.c` poll loop on each successful fetch (uses `u.pct_5h`).
- **Battery** announces when it **falls** into a band: 50/30/20/10/5 → `SPK_BATT_*`. Called from
  `ui.c power_timer_cb` (1 s); **muted on USB**; primes silently on unplug.
- Both **prime silently on first reading** (no boot announcement) with **±3 % hysteresis** so a value on a
  boundary doesn't chatter. A one-shot `SPEAK_BOOT_TEST` (now 0) verified the audio path.

### 13.4 Notes / residual
- Volume maxed (95) + source normalization (was inaudible at 50 cm at vol 70 with un-normalized clips); the
  small speaker has a physical loudness ceiling.
- App bin grew to **~2.49 MB / 3 MB factory partition (21 % free)** from the embedded PCM — watch headroom;
  for more clips, drop the rate (e.g. 11 kHz) or move audio to a SPIFFS/raw partition.
- Possible future: speak the **exact current %** on boot/on-demand (needs 0–100 number-word clips, a bigger
  vocabulary than the 10 fixed-threshold clips).
