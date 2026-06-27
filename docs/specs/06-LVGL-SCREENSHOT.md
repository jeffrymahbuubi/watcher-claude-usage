# 06 — LVGL On-Device Screenshot (Automated UI Verification)

> How to capture the Watcher's **real** LVGL screen as a PNG, with **no MCP and no simulator** —
> so UI changes can be verified automatically instead of asking a human to look at the device.
> **Last updated:** 2026-06-25 · Parent: [SPEC.md](SPEC.md) · Decision [D16] · Tasks T19/T17b/T18 (all done)

## 1. Why this exists
Every UI iteration used to need a human to look at the round screen and report back — slow, and easy to
miss layout bugs (e.g. icons overlapping the arc, T17b). This method makes the device emit a **pixel-exact
PNG of each screen** that the assistant (or any tool) can read directly.

**Chosen over** the `references/Lvgl-mcp-esp32` MCP simulator (Decision **D16**): that sim is **LVGL 9.2**
on a **square 32-bpp** framebuffer, while this device is **LVGL 8.4** on a **round 412×412 RGB565** panel —
the sim can't reproduce our actual output (round mask, our fonts, color-swap), so it can't catch the very
round-edge/overlap bugs we care about. This method is **ground-truth**: it snapshots the live device.

## 2. How it works (pipeline)
```
ESP32-S3 (firmware)                                   Host PC
─────────────────────                                 ─────────────────
lv_snapshot_take_to_buf(screen)   ── UART (COM18) ──► grab_screens.py
  → RGB565 buffer in PSRAM           RLE-hex frames     → decode RLE
  → RLE-hex over the console UART     @@SHOT…@@END        → un-swap RGB565
  (ui_screenshot(), once per boot)                       → write shot_<name>.png
                                                         → assistant Read()s the PNG
```

1. **Built-in LVGL** `lv_snapshot_take_to_buf()` re-renders a screen object into an RGB565 buffer
   (requires `CONFIG_LV_USE_SNAPSHOT=y` — already enabled in `sdkconfig`).
2. Firmware run-length-encodes the buffer and prints it as hex, wrapped in `@@SHOT`/`@@END` markers.
3. A plain host Python script reads the UART, decodes, and writes a PNG. **No MCP server, no PIL** —
   only `pyserial` (bundled in the ESP-IDF python venv) + the stdlib `zlib`/`struct`.

## 3. Firmware components
| File | Role |
|------|------|
| `main/screenshot.c` | `screenshot_dump_all(scrs, names, n)` — snapshot + stream |
| `main/screenshot.h` | declaration |
| `main/ui.c` → `ui_screenshot()` | passes the **5 screens** (`5H`, `7D`, `SERVICE`, `CLAWD`, `SETTINGS`) to `screenshot_dump_all`; `ui_screenshot_one(name)` dumps just the active screen (e.g. `SETUP` during provisioning). *(SETTINGS added in P9/T21.)* |
| `main/main.c` | calls `ui_screenshot()` **once per boot** after the first data fetch |

**WDT-safety (critical):** snapshots are taken **under** `lvgl_port_lock` (fast, into PSRAM), then the lock is
**released** and the slow UART streaming happens unlocked with logs muted
(`esp_log_level_set("*", ESP_LOG_NONE)` → restore after). If you stream while holding the lock, the LVGL task
is starved > 5 s and the task watchdog panics (`task_wdt: LVGL task`).

## 4. Wire protocol (per screen)
```
@@SHOT <name> <w> <h>          ← marker line (e.g. "@@SHOT SERVICE 412 412")
<hex…>                         ← RLE: repeating 8-hex-char groups, 64 groups per line
@@END                          ← end marker
```
Each **8-hex group** = `CCCC` `VVVV`:
- `CCCC` — run length (uint16, count of identical pixels, 1…0xFFFF)
- `VVVV` — the raw 16-bit pixel value **as stored in the framebuffer**

Because the UI is mostly black, RLE shrinks a 412×412×2 = ~340 KB frame to a few KB → fast at 115200 baud.

## 5. Color handling — `LV_COLOR_16_SWAP`
The panel uses RGB565 with `CONFIG_LV_COLOR_16_SWAP=y`, so the two bytes of each pixel are swapped in memory.
The host **un-swaps** before extracting channels:
```python
sw = ((val & 0xFF) << 8) | (val >> 8)      # un-swap
r = (sw >> 11) & 0x1F;  g = (sw >> 5) & 0x3F;  b = sw & 0x1F
R = r*255//31;  G = g*255//63;  B = b*255//31
```

## 6. Usage (the full loop)
From an ESP-IDF-activated shell (see [04-HARDWARE-AND-FLASHING](04-HARDWARE-AND-FLASHING.md) §3):
```powershell
# 1. build + flash (reboot triggers the on-boot screenshot)
idf.py -C C:\esp\watcher-claude-usage -p COM18 -b 460800 flash

# 2. reset the device and capture all screens to PNG
& "C:\Espressif\tools\python\v5.2.1\venv\Scripts\python.exe" `
  C:\esp\watcher-claude-usage\tools\grab_screens.py `
  COM18 "D:\AUNUUN JEFFRY MAHBUUBI\PERSONAL PROJECT\9_SenseCap\docs\screenshots" 90
```
`grab_screens.py <port> <out_dir> <timeout_s>` pulses RTS to reset, reads the frames, and writes
`shot_5H.png`, `shot_7D.png`, `shot_SERVICE.png`, `shot_CLAWD.png`, `shot_SETTINGS.png`. Then open / `Read` those PNGs.
> **Host gotcha:** the read loop is capped at `len(frames) < 5` (one per screen). It was `< 3` (3-screen era,
> silently dropped frames), bumped to `< 4` then **`< 5` when P9 added SETTINGS** — bump it again if you add a 6th screen.

> ⚠️ Don't hold a separate serial monitor open on COM18 at the same time — only one process can own the port.

## 7. Limitations & future work
- **On-boot only:** the firmware dumps once per boot. To re-capture, reflash or power-cycle. A future
  iteration could add a UART **RX command** so the host triggers a capture + selects a screen on demand
  (avoids reflashing; lets you drive navigation from the host).
- **All 5 screens** are dumped each boot; the firmware snapshots the screen objects directly (they need not
  be the active/displayed screen).
- **Baud 115200** — fine thanks to RLE; raise the console baud if you ever disable RLE.

## 8. Where the screenshots live
- **Persistent (committed with the project):** `docs/screenshots/shot_*.png` ← look here first.
- **Per-run default:** wherever you point `grab_screens.py`'s `<out_dir>` argument.
- The assistant's working captures may also be written to the session scratchpad (temporary — lost on
  `/clear`); the persistent copies in `docs/screenshots/` are the ones to keep.

## 9. Related
- Design rationale + as-built: [02-DESIGN](02-DESIGN.md) §11 / §11.1
- Task + build log: [05-IMPLEMENTATION-PLAN](05-IMPLEMENTATION-PLAN.md) T19
- Reusable command also stored in the `sensecap-watcher-firmware-dev` memory.
