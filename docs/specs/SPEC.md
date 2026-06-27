# SPEC — Claude Code Usage Display on SenseCAP Watcher

> **Master / Source-of-Truth document.** This is the entry point and index for the whole
> project. It is a **living document** — keep it updated every work session. When in doubt,
> this file (and the docs it links) is the truth that overrides memory or assumptions.

| | |
|---|---|
| **Project** | Claude Code Usage Display on Seeed SenseCAP Watcher |
| **One-liner** | An always-on, standalone desk display that shows your Claude Code rate-limit usage and service status on the Watcher's round screen. |
| **Status / Phase** | 🟢 **P0–P9 + T24 feature-complete; + P10 UI restyle & LED polish; + P11 spoken alerts (2026-06-27)** — all on-device-confirmed / user-approved, **no open defects**. UI redesign ([07](07-UI-REDESIGN.md)) P1 live (D30); RGB LED off in steady state (D31); **spoken 5H-usage/battery threshold alerts via the ES8311 speaker (D32)**. FR3 per-model still deferred (D11). |
| **Repository** | 🟢 Public on GitHub — **https://github.com/jeffrymahbuubi/watcher-claude-usage** (MIT). Git repo root = `C:\esp\watcher-claude-usage`; this `docs/specs` set is authored in the **D:\…\9_SenseCap** workspace and mirrored into the C: repo before each push (see §8 + the workspace `README.md`). |
| **Last updated** | 2026-06-28 |

---

## 1. Vision & Goal
Turn the SenseCAP Watcher into an ambient "Claude fuel gauge" sitting on the desk: a glanceable
display of how much of the Claude Code rate-limit windows have been consumed and when they reset,
plus whether Claude's service is healthy — all **on-device, with no host computer required**.

Inspired by two open-source ESP32 projects (`oauramos/claude-usage-stick`,
`HermannBjorgvin/Clawdmeter`), re-targeted onto the Watcher's ESP32-S3 + LVGL platform.

## 2. Scope

### In scope
- **5-hour** rate-limit window: utilization % + reset countdown
- **7-day** rate-limit window: utilization % + reset countdown
- **Per-model weekly buckets** (Opus / Sonnet) — ⏸️ **deferred**: not obtainable via the `/v1/messages`
  fallback with a `claude setup-token` (needs the `user:profile` full-login token + refresh). See D11.
- **Model / service status** (Claude service health / incidents)
- **Standalone operation** — device fetches everything itself using the user's OAuth token
- WiFi provisioning + secure on-device token storage
- Custom ESP-IDF + LVGL firmware on the Watcher (reversible reflash)

### Out of scope (with reason)
- **Token counts & $ cost** — dropped. Neither reference project shows them, and they are **not
  obtainable standalone** with an OAuth token (require an org Admin API key, or local `~/.claude`
  JSONL parsing on a host PC). See [03-API-REFERENCE](03-API-REFERENCE.md) §6. (Decision D3.)
- **Host PC sidecar** — not needed once cost is dropped; project is now pure-standalone.
- **Stock SenseCraft AI / camera features** — replaced by our firmware (reversible). Restoring stock
  firmware is documented in [04-HARDWARE-AND-FLASHING](04-HARDWARE-AND-FLASHING.md) §6.

## 3. Status Dashboard

| Workstream | Status | Notes |
|---|---|---|
| Research (reference projects + Watcher) | ✅ Done | See memory + this spec set |
| SPEC documents | 🟡 In progress | This set, v1 |
| Toolchain setup (ESP-IDF) | ✅ Done | ESP-IDF v5.2.1 via EIM-CLI; `idf.py` verified (2026-06-24) |
| Pre-flash device backup (D9) | ✅ Done | Full 32 MB + creds + eFuse @ `C:\esp\watcher_backup` (verified) |
| Flash hello-world (verify display+knob) | ✅ Done | helloworld + lvgl_demo flashed; SPD2010 LCD/touch + knob drivers up (visual confirm pending) |
| WiFi + data fetch | ✅ Working | Live 5h/7d util%+reset+status via `/v1/messages` headers (D11 fallback); status.claude.com 200 |
| LVGL UI | ✅ Iter-2 | 3 knob-nav screens (5H/7D/SERVICE) + SNTP "resets in" countdowns |
| Service status (P4) | ✅ Working | `status.claude.com` summary.json → SERVICE screen real health + incident text (user-confirmed) |
| Stretch visuals (P8) | ✅ Done | FR11 font + FR12 icons (T17b repositioned) + FR13 splash (CLAWD creature) + T19 on-device screenshot |
| Devex — UI verification | ✅ Working | `lv_snapshot`→UART→`tools/grab_screens.py`→PNG (no MCP); assistant Reads PNGs. 02 §11.1 |
| Token security (P6) | ✅ Done | AES-256-GCM in NVS, boot-and-go (no PIN); captive-portal provisioning (real-phone confirmed) |
| Power/polish (P7) | ✅ Done | Stale/offline + keep-last-known; 429 exponential backoff; real battery + charging; configurable brightness + poll interval |
| Settings & Power UX (P9) | ✅ Done | All on-device-confirmed: charging-fix + battery % (DEF1/T20), Settings screen (FR14/T21), press-to-edit brightness persisted (FR15/T22), power-off/reboot w/ confirm + USB-relabel (FR16/T23). 02 §12 |
| UI restyle & LED polish (P10) | ✅ Done | Terracotta titles / brighter text / amber incident (07, P1; P2 size-enlargement reverted as too big, D30); RGB LED off in steady state (D31). User-approved. 07 + D26–D31 |
| Spoken alerts (P11) | ✅ Done | 5H-usage + battery threshold alerts via the ES8311 speaker, offline clips (FR18/D32). User-confirmed audible. 02 §13 |

**Next milestone:** **None required — P0–P11 all complete & on-device-confirmed / user-approved (2026-06-27).** The Watcher is feature-complete per spec. Optional backlog: UI P3 (07 §9 — status-word R8, fetch-error degraded look R10, layout polish R11, raise 5% brightness floor); speak the exact current % on boot/on-demand (needs 0–100 number-word clips); auto-dim/sleep on inactivity; per-model buckets (FR3, blocked by token scope D11); quieten per-poll battery voltage logs. **Watch:** app bin ~2.49 MB / 3 MB factory partition (21% free) after the audio clips. **Known pre-existing:** the on-boot screenshot dump stalls ~10 s + emits a Task-WDT warning when no host is draining the UART (cosmetic; capture via `grab_screens.py` is unaffected).

## 4. Decision Log (ADR-lite, append-only)

| # | Date | Decision | Rationale |
|---|------|----------|-----------|
| D1 | 2026-06-23 | Target the **SenseCAP Watcher** (already owned, connected) | Capable ESP32-S3 + LVGL; open firmware |
| D2 | 2026-06-23 | **Standalone on-device** architecture (stick-style), not host+BLE | User preference; no PC dependency |
| D3 | 2026-06-23 | **Drop tokens + $ cost** | Not shown by either reference project; not obtainable standalone with OAuth alone |
| D4 | 2026-06-23 | Display set = 5h/7d util%+reset, per-model weekly buckets, service status | Matches what's proven + standalone-fetchable |
| D5 | 2026-06-23 | Custom **ESP-IDF + LVGL** firmware from `examples/helloworld`/`lvgl_demo` | Only supported path; no Arduino BSP for Watcher |
| D6 | 2026-06-23 | Primary data source = `GET /api/oauth/usage`; `/v1/messages` headers as fallback; `status.claude.com` for status | oauth/usage is richer (per-model buckets) |
| D7 | 2026-06-23 | Toolchain = **ESP-IDF (not PlatformIO/Arduino)** | Watcher firmware is a native ESP-IDF project |
| D8 | 2026-06-24 | Clone buildable firmware to **`C:\esp\SenseCAP-Watcher-Firmware`** (space-free), not in-project `references/` | Project path has spaces + length → breaks ESP-IDF/CMake/Ninja builds |
| D9 | 2026-06-24 | **Mandatory device-identity backup before ANY flash**: dump `nvsfactory`@0x9000 (200K) + full 32 MB image + eFuse summary; store outside repo | Unique SenseCraft credentials (SN/EUI/keys) are NOT in the public factory image → unrecoverable if erased (see 04 §6.1) |
| D10 | 2026-06-24 | Custom firmware lives at **`C:\esp\watcher-claude-usage`** (standalone, space-free), separate from the cloned reference repo; WiFi via raw `esp_wifi` (BSP has none); dev uses hardcoded creds in git-ignored `secrets.h`; real provisioning deferred to T6 | Keeps reference repo pristine; fastest path to validate the network before UI; BSP exposes no reusable WiFi/provisioning |
| D11 | 2026-06-24 | `/api/oauth/usage` is **not usable with a `claude setup-token`** (HTTP 403 — needs `user:profile` scope it lacks); fall back to **`/v1/messages` rate-limit headers** (per D6) for 5h/7d | Verified on-device; setup-token = inference scope only. Trade-off: no per-model Opus/Sonnet buckets + a tiny probe cost per poll. The rich endpoint would need the full interactive-login token + on-device refresh (SPEC §5). |
| D12 | 2026-06-24 | **P4 service status** = unauthenticated GET `status.claude.com/api/v2/summary.json`, parsed with **cJSON** (one call yields both overall `indicator`/`description` and the `incidents` array). SERVICE screen repurposed from rate-limit `unified-status` to real claude.com health | One call, no quota cost, single source of truth; richer than `status.json` alone (also carries incident names). |
| D13 | 2026-06-24 | **P8 stretch visuals planned** (FR11 extended-glyph font · FR12 Lucide icons · FR13 splash animation) adapting `references/Clawdmeter`. Font scope = **punctuation-only (no CJK)**; splash asset source **deferred**; sequence **per-phase**. Interim `ascii_sanitize()` shim transliterates incident punctuation until FR11 lands | User decisions 2026-06-24. **Our LVGL is 8.4.0** (vs Clawdmeter's LVGL 9) → `lv_font_conv`/image C output drops in without the LVGL-9 patches its README requires. |
| D14 | 2026-06-24 | **T17 icons = `LV_IMG_CF_ALPHA_8BIT`** (alpha-only, recolored at runtime) generated `lucide-static` SVG → `sharp` raster → C array. **T16 font** generated via `lv_font_conv` from LVGL's own `Montserrat-Medium.ttf`; `ascii_sanitize` shim removed | Alpha-8bit halves size vs RGB565A8 and lets one bitmap serve all color states. T16 confirmed LVGL-8.4 needs zero patching. Splash assets (T18) = **claudepix Clawd sprites** (gray-area, personal use). |
| D15 | 2026-06-24 | **T18 splash assets = claudepix Clawd sprites** (deferred decision now resolved) | User pick; authentic mascot. Anthropic-copyright gray area, acceptable for a personal device. |
| D16 | 2026-06-24 | **Automated UI verification = Option B** (on-device `lv_snapshot` → UART → host `grab_screens.py` → PNG), **not** the `Lvgl-mcp-esp32` MCP sim | Ground-truth: our real round 412×412 RGB565 LVGL **8.4** device. The MCP sim is LVGL **9.2** + square 32bpp → wouldn't match (can't catch round-edge/arc-overlap bugs). No MCP/extra runtime. Implemented & proven (02 §11.1). |
| D17 | 2026-06-24 | **P6 provisioning = captive-portal SoftAP** (phone web form), **not** on-device knob entry | The ~100-char OAuth token is impractical to enter on the knob. Open AP `Watcher-Setup` + captive DNS + `esp_http_server` form. Re-provision via knob long-press / hold-at-boot. |
| D18 | 2026-06-24 | **P6 token security = boot-and-go AES-256-GCM software obfuscation, NO PIN** (original PIN-KDF design dropped) | Headless ambient device — a PIN at every power-cycle defeats the always-on use case (user decision). Key = SHA-256(MAC‖salt‖pepper); auto-decrypts on boot. **Tradeoff:** AC6 only partially met (resists casual `strings` dump, not a determined same-chip attacker). Full at-rest protection = HW flash-encryption (irreversible eFuse), deliberately not burned to keep the restore path. 02 §5.1. |
| D19 | 2026-06-25 | **Charging icon (DEF1) = derive from `BSP_PWR_VBUS_IN_DET`, not `bsp_system_is_charging`/`CHRG_DET`** | The BSP `is_charging` reads the wrong pin + inverts (true when CHRG_DET high = unplugged). Stock firmware uses `VBUS_IN_DET==0` everywhere. Fix in our `main.c` (no BSP edit). 02 §12.1. |
| D20 | 2026-06-25 | **Shutdown = true power-off via `bsp_system_shutdown()`** (cuts BSP_PWR_SYSTEM); wake = hold wheel ~3 s or USB | User decision; schematic-verified recoverable (soft-latch re-asserted at boot by `bsp_io_expander_init`), no reflash. Guards: confirm gesture, USB-present → relabel "Reboot" (true off no-ops on USB), show wake instructions. 02 §12.2. |
| D21 | 2026-06-25 | **Settings = 5th knob screen, single `lv_group`, press-to-edit brightness** (not sub-group swap, not touch-only) | LVGL 8.4 editable(slider)/non-editable(screen) model makes it work in one group; non-sticky given ≥2 members. Brightness persists via a lightweight NVS `bri`-only write (not `config_save`, which re-encrypts the token). MUST-FIX: reprovision long-press vs editable slider. 02 §12.3. |
| D22 | 2026-06-25 | **Battery badge = self-driven `lv_timer` (1 s), decoupled from the network poll; show numeric %; drop `bsp_battery_is_present()` gating** | T20 on-device feedback: when battery updated only inside the 60–600 s poll loop, re-plug didn't refresh and the unplugged glyph was an empty grey outline. Mirrors stock `app_device_info.c` (fast VBUS check + 30 s percent + immediate re-measure on unplug). USB state read every tick (responsive both ways); percent every 30 s / on transition (ADC is log-spammy). Numeric `NN%` added (user request). 02 §12.1. |
| D23 | 2026-06-25 | **Settings/power-off as-built: custom top-layer confirm modal (temporary encoder group, Cancel default); battery % placed BESIDE the icon (not below)** | Power-off confirm uses `lv_layer_top()` + a throwaway `lv_group` for the two buttons (clean Cancel-default focus + restore on close) rather than `lv_msgbox` (finicky encoder nav). Button live-relabels Reboot/Power-Off by USB state. Battery % was first placed below the icon but crowded the high SERVICE title on the round panel (user feedback) → moved to `LV_ALIGN_OUT_RIGHT_MID` (status-bar style) globally. 02 §12.2/§12.3. |
| D24 | 2026-06-25 | **Battery % returned to BELOW the icon; the "SERVICE" screen title removed instead** (supersedes the %-placement part of D23) | User preferred this over the status-bar layout: revert `batt_lbl` to `LV_ALIGN_OUT_BOTTOM_MID` and delete the "SERVICE" label from `build_status()`. With no title the % below the icon has clear space; the health glyph + "All Systems Operational" still identify the screen. The confirm-modal part of D23 is unchanged. 02 §12.3. |
| D25 | 2026-06-25 | **Fix DEF2 (wake re-login) by REMOVING the boot `knob_held_at_boot()` factory-reset trigger** (✅ implemented & on-device-confirmed = T24) | The boot-time hold-knob→reprovision gesture (P6/D17) collides with the hold-wheel-to-wake gesture (T23/D20) — the wheel is still held as the device boots, so the device factory-resets itself and re-asks WiFi login. Boot-reprovision is now net-harmful; the in-use 3 s long-press (and a re-flash / NVS erase) remain as reprovision paths. Chosen over distinguishing wake-vs-reprovision holds (fragile) or moving reprovision into Settings (more work). 02 §12.2. |
| D26 | 2026-06-27 | **Adopt a moderate UI-redesign initiative** (legibility for *both* arm's-length + across-the-room, semantic colour system, reclaim the empty lower half; dark theme kept) — 🟠 **proposed, no code yet** | 4-lens UI review (legibility/visual/LVGL-feasibility/semantics) found the hero symbols 3–4× too small at distance, secondary text too dim, and colour carrying meaning alone (incl. the incident-shown-green inversion). Spec = [07-UI-REDESIGN](07-UI-REDESIGN.md). |
| D27 | 2026-06-27 | **All screen titles = brand terracotta `#D97757`** (5H/7D/SERVICE/SETTINGS); **blue reserved exclusively for the `maintenance` service state** (supersedes 5H-green/7D-blue titles) | Separates *chrome* (terracotta) from *data* (green/amber/red ramp) from *text* (white/grey) so green stops doing five jobs. User decision. 07 §3/§4. |
| D28 | 2026-06-27 | **Icon scale = "moderate": top-bar WiFi/battery 24→~44–48 px, SERVICE health glyph 30→~80 px, via regenerating crisp bitmaps** (`gen_icons.mjs`), not runtime zoom | Room-readable without crowding the hub / colliding with the 360 px ring (the aggressive 64–72 px option) and without the soft edges of `lv_img_set_zoom`. Re-tune `add_topbar` y-offset to clear the arc. User decision. 07 §6/§3. (As-built: top-bar landed at **40 px** for ring clearance; health 80 px. 07 §14.) |
| D29 | 2026-06-27 | **Battery % moved to the RIGHT of the icon (horizontal `wifi · batt · NN%` top cluster) — supersedes the below-placement of D24** | The enlarged 40 px top-bar icons (D28/R6) made the stacked %-below collide with the gauge title (5H/7D) and the 80 px SERVICE health glyph. Right-placement keeps the bigger icons in a compact one-line cluster that clears both. `add_topbar`/`render_battery` use `LV_ALIGN_OUT_RIGHT_MID`. 07 §14. **(⚠️ REVERTED by D30.)** |
| D30 | 2026-06-27 | **Reverted P2 (the size enlargement) — back to P1 sizes**: icons 24/30 px, SERVICE status font 22/16, **battery % below the icon (D24 restored, D29 reverted)**; D28/R6/R9 tried-and-rolled-back | User saw P2 on-device and found the enlarged wifi/battery icons + bigger SERVICE text too large; preferred the prior (P1) look. All P1 colour/semantic wins kept (terracotta titles @28, brighter text, amber incident). `MONTSERRAT_28` stays (titles); `font_ext_28.c` removed; `tools/icontool/` now regenerates the 24/30 icons. 07 §15. |
| D31 | 2026-06-27 | **RGB LED OFF in steady state** — removed the per-poll usage-level cue; keep only brief boot/setup (magenta) + connecting (blue) colors | The single WS2812 RGB LED (GPIO40 `BSP_RGB_CTRL`, BSP `bsp_rgb_set`) was set every poll in `main.c` to mirror the worse-of 5h/7d level (green<70 / amber≥70 / red≥90) and **never turned off** → continuous ~4–7 mA battery drain, redundant with the on-screen arc/%/zap. `main.c` now calls `bsp_rgb_set(0,0,0)` once before the poll loop and no longer recolors per poll (429 backoff kept). User decision; on-device LED-off confirmed by user. |
| D32 | 2026-06-27 | **Spoken usage/battery alerts** via the on-board **ES8311 speaker** — **offline pre-recorded clips** (Windows SAPI "Zira" → 16 kHz/16-bit mono PCM, peak-normalized + soft-limited for loudness, embedded as C arrays), played by a dedicated `speak` task; announced automatically on threshold crossings (**5H usage rising** into 25/50/75/90/100; **battery falling** into 50/30/20/10/5, muted on USB) with prime-silent-on-boot + hysteresis | User wanted the device to "speak". Offline clips chosen over cloud-TTS (keeps the no-host/standalone design, D2) and over on-device TTS (ESP-IDF English TTS not viable). Volume maxed (95 = BSP cap) + source normalization, since it was too quiet at 50 cm. `speak.c/.h` + `speak_clips.c` (gen: `tools/audio/gen_clips.ps1` + `wav_to_c.py`); hooks in `main.c` (usage) + `ui.c` power_timer (battery). Chunked I2S writes (a single big `esp_codec_dev_write` tripped the Task-WDT). User-confirmed audible. 02 §13. |
| D33 | 2026-06-28 | **Published to GitHub as a public MIT repo** — repo root `C:\esp\watcher-claude-usage` → https://github.com/jeffrymahbuubi/watcher-claude-usage. The `D:\…\9_SenseCap` folder stays the local **workspace** (claude-flow memory + live `docs/specs` authoring); specs are mirrored into the C: repo before each commit/push | Keeps ESP-IDF builds on the space-free C: path (D8/D10) while preserving the existing claude-flow workspace. `secrets.h` stays git-ignored (only `secrets.h.template` shipped); added README + LICENSE (MIT) + expanded `.gitignore` (build/, managed_components/, node_modules/, *.bin). gh CLI as `jeffrymahbuubi`. |

## 5. Open Questions / Risks
- ✅ **RESOLVED (2026-06-25) — DEF2 (wake → WiFi re-login):** after a **true power-off**, waking by **holding the wheel ~3 s** collided with the boot-time **hold-knob→factory-reset** check (`knob_held_at_boot()` in `main.c`) → wiped the NVS creds → device re-asked WiFi login on every wake. **Fixed in T24 (D25): deleted the boot `knob_held_at_boot()` factory-reset trigger + its `app_main()` call** — provisioned devices now boot straight to normal op on wake. On-device-confirmed (battery: power off → hold wheel → gauges return, no portal). Reprovision remains via the in-use 3 s knob long-press.
- ✅ RESOLVED (2026-06-24): ESP32-S3 prog/flash UART = **COM18** (COM19 = no response). Baud **460800** (921600 corrupts on the CH342). Flash-encryption & secure-boot **disabled**; MAC `8c:bf:ea:09:b0:08`.
- ✅ RESOLVED (2026-06-24): panel driver = **SPD2010** (round 412×412 LCD + capacitive touch combo), RGB565/16-bit (`LV_COLOR_16_SWAP`). Knob encoder = GPIO41(A)/42(B) + button; confirmed via `lvgl_demo` serial.
- 🟡 PARTIAL (2026-06-24): `claude setup-token` = long-lived but **inference scope only** (no `user:profile` → `/api/oauth/usage` 403, see D11). Full-login token (`~/.claude/.credentials.json`) HAS `user:profile` + a `refreshToken`, but the access token **expires ~hourly**. On-device refresh is feasible (refresh_token grant + Claude Code public client_id), **but refresh-token rotation would likely break the PC's own Claude Code login** unless the device self-authorizes (its own flow). → leaning toward the `/v1/messages` fallback (long-lived setup-token, no refresh).
- 🟡 PARTIAL: we use `/v1/messages` response headers (D11), not `/api/oauth/usage`. `User-Agent: claude-code/2.1.187` works (HTTP 200, no 429); exact UA-version sensitivity untested beyond "present + `claude-code/<ver>`".
- ✅ RESOLVED: round layout uses LVGL **arcs** (270° gauge) + center % label; 3 screens (5H / 7D / SERVICE), knob-navigated.
- ⚠️ Dependency on **undocumented** Anthropic endpoints/headers — could change without notice.

## 6. Document Index
| File | Purpose | Last updated |
|------|---------|--------------|
| [01-REQUIREMENTS](01-REQUIREMENTS.md) | What the product must do + acceptance criteria | 2026-06-25 |
| [02-DESIGN](02-DESIGN.md) | Architecture, components, UI, security (P9 feasibility §12) | 2026-06-25 |
| [03-API-REFERENCE](03-API-REFERENCE.md) | Anthropic data-source endpoints + schemas | 2026-06-24 |
| [04-HARDWARE-AND-FLASHING](04-HARDWARE-AND-FLASHING.md) | Device specs, toolchain, build/flash/recovery | 2026-06-24 |
| [05-IMPLEMENTATION-PLAN](05-IMPLEMENTATION-PLAN.md) | Roadmap, task board, progress log | 2026-06-25 |
| [06-LVGL-SCREENSHOT](06-LVGL-SCREENSHOT.md) | On-device LVGL screenshot method (automated UI verification, no MCP) | 2026-06-25 |
| [07-UI-REDESIGN](07-UI-REDESIGN.md) | 🟢 Legibility/semantic restyle — **P1 LIVE** (terracotta titles@28, brighter text, amber incident, small icons); P2 size-enlargement tried & **reverted as too big** (D30); P3 optional | 2026-06-27 |

## 7. Glossary
- **OAuth token** — Claude Code credential from `claude setup-token`; authorizes usage queries.
- **Rate-limit window** — 5-hour and 7-day quota windows; utilization is 0–100%.
- **ESP-IDF** — Espressif IoT Development Framework (the Watcher's native SDK).
- **LVGL** — Light & Versatile Graphics Library; the Watcher's UI framework.
- **NVS** — Non-Volatile Storage (ESP32 flash key-value store; where the encrypted token lives).
- **CH342** — WCH dual USB-to-UART bridge on the Watcher's bottom USB-C (COM18/COM19 here).
- **SSCMA** — Seeed's AI inference stack driving the Himax chip (unused by this project).

## 8. Living-Document Update Protocol
- **Repo locations (D33):** canonical git repo = `C:\esp\watcher-claude-usage` (GitHub, public). Specs are authored in the **D:\…\9_SenseCap** workspace; **after any spec edit, mirror `docs/specs` (and `docs/screenshots`) → the C: repo, then `git commit` + `git push`.** See the workspace `README.md` for the exact sync command.
- **Every work session:** update §3 Status Dashboard + `05` task board + `05` build log.
- **New decision:** append a row to §4 Decision Log (never edit/delete past rows).
- **Scope change:** edit §2 here + `01-REQUIREMENTS`, and log it in §4.
- **Design change:** edit `02-DESIGN`, note it in `05` build log.
- Keep the **Last updated** date current at the top of each file.

## 9. Spec Changelog
| Date | Change |
|------|--------|
| 2026-06-23 | Initial 6-file spec set created from research phase. |
| 2026-06-24 | Implemented P0–P3 + P5 (toolchain, HW + backup, WiFi/HTTPS, `/v1/messages` fetch, 3-screen LVGL UI). Added decisions D8–D11. Reconciled 01/02/03/04 with the `/v1/messages` pivot (D11), SPD2010/COM18 facts, and the `C:\esp\watcher-claude-usage` build. |
| 2026-06-24 | **P4 done** (D12): `claude_status.c` + cJSON → SERVICE screen shows live `status.claude.com` health + incidents (user-confirmed). Fixed UTF-8 punctuation boxes via `ascii_sanitize`. **Planned P8 stretch visuals** (D13): added FR11/FR12/FR13 (01), design §10 (02), tasks T16–T18 + T9/T9b done (05). Noted LVGL 8.4.0 vs Clawdmeter LVGL 9. |
| 2026-06-24 | **P8 partial — T16 + T17 done** (D14): FR11 extended font (`font_ext_16/22.c`, shim removed) + FR12 Lucide icons (`icons_lucide.c/.h`, alpha-8bit, recolored). Logged **T17b** (icons overlap usage arc — reposition) + **T19** (automated UI verification: `Lvgl-mcp-esp32` sim vs on-device serial screenshot — added design §11). Resolved T18 splash assets = claudepix (D15). |
| 2026-06-24 | **T19 done — Option B chosen + implemented** (D16): on-device `lv_snapshot` → `screenshot.c` → UART RLE-hex → host `tools/grab_screens.py` → PNG (no MCP). Proven on all 3 screens; assistant Reads the PNGs. As-built in 02 §11.1. T17b confirmed via screenshot (+ pale-WiFi-on-SERVICE observed). |
| 2026-06-24 | Added dedicated method doc **[06-LVGL-SCREENSHOT](06-LVGL-SCREENSHOT.md)** (full how-to) + indexed it here. Persisted the 3 captured screens to **`docs/screenshots/shot_*.png`** (scratchpad copies are temporary). |
| 2026-06-25 | **P9 started — T20 ✅ done & user-confirmed** (D19 + D22): charging-icon fix (`VBUS_IN_DET`) + battery badge rework (self-driven `lv_timer`, numeric %, prompt plug/unplug, dropped `is_present`). Updated 01 (DEF1 fixed), 02 §12.1 (as-built), 05 (T20 ✅ + build log). Next: T21 Settings scaffold. |
| 2026-06-25 | **✅ P9 COMPLETE — T21+T22+T23 done & on-device-confirmed** (D20/D21/D23): Settings 5th screen (FR14) + press-to-edit brightness persisted via `config_set_brightness` (FR15) + power-off/reboot confirm modal with USB-relabel (FR16). Then UI tweak: battery % moved beside the icon (fixes SERVICE crowding). Updated 01 (FR14/15/16 ✅), 02 §12.2/§12.3 (as-built), 05 (T21–23 ✅ + build log), SPEC §3/decisions/changelog. **Project feature-complete (P0–P9).** |
| 2026-06-25 | **UI polish (D24, supersedes %-placement of D23):** battery % returned to BELOW the icon + the "SERVICE" title removed (user preference over the status-bar layout). `ui.c` only; screenshot-verified on 5H + SERVICE. |
| 2026-06-25 | **Discovered DEF2 (wake → re-login) + decided fix D25** (impl next session, T24): true power-off + hold-wheel-to-wake collides with the boot `knob_held_at_boot()` factory-reset → accidental NVS wipe. No code change this session; logged in 01 (DEF2), 02 §12.2, 05 (T24 + build log), §5 risks, D25. **P0–P9 remain feature-complete; DEF2 is the sole queued defect.** |
| 2026-06-25 | **✅ DEF2 FIXED (T24 / D25) — on-device-confirmed.** Removed `knob_held_at_boot()` + its `app_main()` call from `main.c` so a provisioned device boots straight to normal op on wake; the boot factory-reset no longer collides with hold-wheel-to-wake (in-use 3 s knob long-press kept as the sole reprovision gesture). Built clean (47% free) + flashed COM18; **user confirmed on battery** (power off → hold wheel → gauges return, no WiFi portal). Updated 01 (DEF2 ✅), 02 §12.2, 05 (T24 ✅ + changelog), §5/D25. **P0–P9 + T24 feature-complete; no open defects.** |
| 2026-06-27 | **UI-redesign spec drafted (D26–D28) — 🟠 proposed, no code changed.** Captured all 5 live screens off the device + ran a 4-lens UI review (legibility / visual-hierarchy / LVGL-feasibility / status-semantics). New doc **[07-UI-REDESIGN](07-UI-REDESIGN.md)**: semantic palette, 4-step type scale, per-screen layout, the incident-green + grey-overload + fetch-error-invisible semantic fixes, and a phased R1–R11 plan (P1 = cheap recompile-only wins). Decisions: terracotta titles (D27), moderate icon scale (D28). Indexed here (§6) + logged (§4). **Implementation deferred to a later session.** |
| 2026-06-27 | **✅ UI-redesign P1 + cheap-P2 IMPLEMENTED & on-device-confirmed.** `ui.c` + `sdkconfig` (enabled `MONTSERRAT_28`): semantic palette constants (R7); terracotta `#D97757` titles @28 (R5/D27); secondary text `#9E9E9E`→`#C8C8C8` (R1); countdown/battery%/settings 16→22 px (R2); **SERVICE incident decoupled from indicator → amber not green** (R3/§8.1); zap = fixed terracotta + wifi-disconnect amber-not-red (R4); terracotta slider + danger button (R11 partial). Built clean (46 % free), flashed COM18, captured 5 screens → `docs/screenshots/` (now the redesigned UI). As-built in 07 §13. **Pending P2/P3:** icon enlargement (R6), bigger live status font (R9), fetch-error degraded look (R10), layout repositioning (R8/R11). |
| 2026-06-27 | **✅ UI-redesign P2 IMPLEMENTED & on-device-confirmed.** R6 icon enlargement — rebuilt + persisted `tools/icontool/` (lucide-static→sharp→alpha-8 `gen_icons.mjs`); top-bar 24→40 px, SERVICE health glyph 30→**80 px**, zap→28. R9 bigger live status font — generated `font_ext_28.c` (lv_font_conv), SERVICE description 22→28, incident 16→22. Layout re-tuned: **battery % moved beside the icon** (horizontal `wifi·batt·NN%` cluster, **D29 supersedes D24**) to clear the title / health-glyph collisions the bigger icons caused; SERVICE elements re-spaced. Built clean (45 % free), flashed COM18, 5 screens → `docs/screenshots/`. As-built 07 §14. **Remaining (P3, optional):** R8 status-word under %, R10 fetch-error degraded look on both gauges, R11 countdown-into-ring-mouth + CLAWD caption, 5 % brightness floor. |
| 2026-06-27 | **↩️ UI-redesign P2 REVERTED per user feedback (D30) — live UI back to P1.** User found the enlarged icons (40/80) + bigger SERVICE text too big; preferred the previous look. Regenerated icons at **24/30** (`tools/icontool`), reverted SERVICE status font to **22/16** + battery **% below the icon** (D24 restored, D29 reverted), removed `font_ext_28.c`. `MONTSERRAT_28` kept (P1 terracotta titles). Built/flashed/screenshot-verified = P1. **Live UI = P1** (colour/semantic redesign only: terracotta titles, brighter text, amber-not-green incident, fixed-accent zap, terracotta slider). 07 §15. |
| 2026-06-27 | **✅ RGB LED steady-state drain removed (D31).** Validated the device has **1× WS2812 RGB LED** (GPIO40 `BSP_RGB_CTRL`; BSP `bsp_rgb_init`/`bsp_rgb_set`, `sensecap-watcher.c:296-326`); it was driven every poll in `main.c` to mirror usage level and never turned off (always-on, redundant with the screen). `main.c`: removed the per-poll `bsp_rgb_set` level/error calls (kept 429 backoff) + added `bsp_rgb_set(0,0,0)` before the poll loop → LED dark in normal operation; brief boot/setup magenta + connecting blue kept. Built/flashed COM18. **User-confirmed LED off.** |
| 2026-06-27 | **✅ P11 — Spoken usage/battery alerts (D32), user-confirmed audible.** Validated the Watcher has an **ES8311 speaker** (04 Audio row). Added offline TTS: 10 pre-recorded "Zira" clips (16 kHz mono PCM, peak-normalized + soft-limited for loudness, vol 95) generated by `tools/audio/{gen_clips.ps1, wav_to_c.py}` → embedded `main/speak_clips.c`; `main/speak.c` plays them in a dedicated task with **chunked** I2S writes (a single ~100 KB `esp_codec_dev_write` tripped the Task-WDT). Threshold logic: 5H usage rising (25/50/75/90/100) + battery falling (50/30/20/10/5, muted on USB), prime-silent-on-boot + hysteresis; hooked into the poll loop (`main.c`, usage) + battery timer (`ui.c power_timer_cb`). User confirmed audible + loud. ⚠️ App bin now **~2.49 MB / 3 MB factory partition (21 % free)** — the audio clips are large; watch headroom before adding more. 02 §13. |
| 2026-06-28 | **📦 Published to GitHub (D33).** Repo published public + MIT at `C:\esp\watcher-claude-usage` → https://github.com/jeffrymahbuubi/watcher-claude-usage (73 files; `secrets.h` excluded — only `secrets.h.template` shipped). Added repo `README.md` + `LICENSE` + expanded `.gitignore`; copied `docs/specs` + `docs/screenshots` into the repo. Established the workspace model: author specs in the **D:\…\9_SenseCap** workspace, mirror → C: repo, commit/push. Added a workspace `README.md` (D:) explaining the split. |
