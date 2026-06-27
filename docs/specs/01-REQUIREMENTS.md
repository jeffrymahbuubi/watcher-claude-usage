# 01 — Requirements

> What the product must do. Defines "done" independent of implementation.
> **Last updated:** 2026-06-27 · Parent: [SPEC.md](SPEC.md)

## 1. Overview / Persona
A solo developer who uses Claude Code daily wants an **at-a-glance desk display** of how much of
their Claude rate-limit windows is left and when they reset — without opening a terminal or app, and
without a companion program running on the PC.

## 2. Functional Requirements

| ID | Requirement | Priority |
|----|-------------|----------|
| FR1 | Display **5-hour** window utilization (%) and time-to-reset | Must |
| FR2 | Display **7-day** window utilization (%) and time-to-reset | Must |
| FR3 | Display **per-model weekly buckets** (Opus / Sonnet) when the API returns them | Should · ⏸️ deferred (D11) |
| FR4 | Display **Claude service / model status** (healthy / incident) | Should |
| FR5 | **Knob navigation** between screens (and/or touch) | Must |
| FR6 | **WiFi provisioning** flow for first-time setup (no hardcoded creds) | Must |
| FR7 | **Token setup** — accept the Claude OAuth token during provisioning | Must |
| FR8 | **Configurable poll interval** (default conservative, e.g. 60–120 s) | Should |
| FR9 | **Visual states**: loading, last-known value, error/offline | Must |
| FR10 | Persist config (token, WiFi, interval) across reboots | Must |
| FR11 | **Extended-glyph font** — render the punctuation present in live API/incident text (curly quotes, en/em dash, ellipsis); no empty boxes | Could · stretch (P8) |
| FR12 | **Lucide icon set** — small status glyphs (WiFi, battery, service health) instead of text-only labels | Could · stretch (P8) |
| FR13 | **Splash / ambient animation** — pixel-art animation screen that grows busier as usage climbs | Could · stretch (P8) |
| FR14 | **On-device Settings screen** — 5th knob-nav screen for device controls | ✅ Done · P9/T21 (02 §12.3) |
| FR15 | **On-device brightness control** — adjust LCD brightness from Settings (press-to-edit), persisted | ✅ Done · P9/T22 (02 §12.3) |
| FR16 | **Power off** — shut the device down from Settings (true power-off, confirm gesture, USB→reboot) | ✅ Done · P9/T23 (02 §12.2) |
| DEF1 | **Defect:** battery icon shows "charging" when USB-C unplugged (reads `CHRG_DET` not `VBUS_IN_DET`) | ✅ Fixed · P9/T20 (02 §12.1) |
| FR17 | **Battery %** — show numeric remaining-charge % + level icon, updated promptly on plug/unplug | ✅ Done · P9/T20 (02 §12.1) |
| DEF2 | **Defect:** after true power-off, waking via the wheel re-asks WiFi login — the hold-wheel-to-wake gesture collides with the boot `knob_held_at_boot()` factory-reset, wiping NVS creds | ✅ Fixed · T24 (D25 — boot trigger removed; on-device-confirmed 2026-06-25; 02 §12.2) |
| FR18 | **Spoken alerts** — speak 5H-usage and battery threshold crossings aloud via the on-board speaker (offline, no host) | Could · ✅ Done · P11/T27 (D32; 02 §13) |

## 3. Non-Functional Requirements

| ID | Requirement |
|----|-------------|
| NFR1 | **Security** — OAuth token stored **encrypted at rest** in NVS, protected by a PIN; never in plaintext or source. |
| NFR2 | **Reliability** — on API/WiFi failure, show last-known reading + a clear stale/error indicator; never crash-loop. |
| NFR3 | **Rate-limit friendliness** — polling must not meaningfully burn the user's own quota; respect 429 with backoff. |
| NFR4 | **Power** — reasonable battery behavior; allow screen dimming; tolerate USB-powered always-on use. The status **RGB LED is off in steady state** (was always-on mirroring usage level → redundant drain; D31). |
| NFR5 | **Legibility** — UI designed for the **round 412×412** panel; key numbers readable at a glance. |
| NFR6 | **Maintainability** — modular firmware; external API details isolated in `03-API-REFERENCE`. |
| NFR7 | **Recoverability** — stock firmware restorable; custom build reproducible from the repo. |

## 4. Constraints
- **WiFi 2.4 GHz only** (no 5 GHz radio).
- **Data limited to what the OAuth token can fetch** — no tokens/$ cost (see scope).
- **Round 412×412, 1.45"** display — design for a circle, corners unusable.
- **ESP-IDF only** — no Arduino board support for the Watcher.
- Depends on **undocumented** Anthropic endpoints/headers (acceptable risk, documented).

## 5. Assumptions
- User has an active Claude subscription (Pro/Max) usable with Claude Code.
- User can run `claude setup-token` to obtain an OAuth token.
- A 2.4 GHz WiFi network is available where the device sits.
- User accepts replacing stock firmware (reversible).

## 6. Acceptance Criteria
- **AC1 (FR1/FR2):** With a valid token + WiFi, the device shows non-stale 5h and 7d utilization %
  and reset countdowns that match Claude Code's own `/usage` within one poll interval.
- **AC2 (FR4):** When Claude reports an incident, the status screen reflects it; otherwise shows healthy.
- **AC3 (FR5):** Rotating/pressing the knob changes screens predictably.
- **AC4 (FR6/FR7):** A fresh device with no config can be provisioned (WiFi + token) without reflashing.
- **AC5 (FR9/NFR2):** Pulling WiFi shows an offline/stale indicator and recovers automatically on reconnect.
- **AC6 (NFR1):** Token is unreadable from a flash dump without the PIN.

## 6.5 Implementation Status (2026-06-25)
- **Met (P0–P5):** FR1, FR2 (incl. SNTP "resets in" countdowns), **FR4 ✅ full** (`status.claude.com` summary.json → SERVICE screen shows real service health + incident text), FR5 (knob cycles 5H/7D/SERVICE), FR8 (poll loop at fixed 60 s; *configurable* = todo), FR9 (basic error state; full stale/last-known = todo).
- **P8 done:** **FR11 ✅** extended-glyph font; **FR12 ✅** Lucide icons (T17b ✅ repositioned clear of the arc); **FR13 ✅** splash/ambient animation (4th CLAWD creature screen, mood by usage; 02 §10.3).
- **P6 done (2026-06-24):** **FR6 ✅** WiFi provisioning + **FR7 ✅** token setup via captive-portal SoftAP (02 §5.2); **FR10 ✅** config persisted in NVS; **NFR1 ⚠️ partial** — token stored AES-256-GCM-encrypted at rest (no compiled secrets), but **boot-and-go (no PIN)** per user decision → AC6 only partially met (resists casual dump, not a determined same-chip attacker; 02 §5.1).
- **P7 done (2026-06-24):** **FR9 ✅ full** stale/last-known + offline states (`ui_set_stale`, WiFi-red, keep last-known); **NFR2 ✅** never crash-loop (background auto-reconnect, keep last-known); **NFR3 ✅** 429 exponential backoff; **NFR4 ✅** real battery (`bsp_battery_*`) + configurable brightness (`bsp_lcd_brightness_set`); **FR8 ✅** configurable poll interval (`cfg.poll_s`). Auto-dim/sleep deferred (backlog).
- **P9 — ✅ COMPLETE (02 §12), all on-device-confirmed 2026-06-25:** **DEF1 ✅** charging-icon fix + **FR17 ✅** battery % (T20), **FR14 ✅** Settings screen (T21), **FR15 ✅** on-device brightness, persisted (T22), **FR16 ✅** power-off/reboot w/ confirm + USB-relabel (T23). Battery % sits below the icon and the "SERVICE" title was removed (D24) after spacing feedback. **All Must + all Should now met EXCEPT FR3** (per-model, blocked by token scope D11).
- **Deferred:** FR3 per-model (D11 — needs `user:profile` token). Devex: **T19** automated UI verification (02 §11). **All Must + all Should are met EXCEPT FR3 (blocked by token scope, D11); no open defects — DEF1 & DEF2 both fixed (P9 / T24).**
- Live status: [05-IMPLEMENTATION-PLAN](05-IMPLEMENTATION-PLAN.md) task board.

## 7. Out of Scope
Mirrors [SPEC.md](SPEC.md) §2: tokens, $ cost, host-PC sidecar, stock AI/camera features.
