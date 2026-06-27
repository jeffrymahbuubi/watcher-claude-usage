# 04 — Hardware, Toolchain & Flashing

> Device specs, dev environment, and how to build / flash / recover.
> **Last updated:** 2026-06-27 · Parent: [SPEC.md](SPEC.md)

## 1. Hardware Spec (SenseCAP Watcher)
| Component | Spec |
|-----------|------|
| **Main MCU** | ESP32-S3 @ 240 MHz, **8 MB PSRAM**, dual-core Xtensa LX7 |
| **AI co-processor** | Himax HX6538 (Cortex-M55 + Ethos-U55), 16 MB flash — **unused by this project** |
| **ESP32-S3 flash** | 32 MB SPI flash |
| **Display** | **412 × 412 round**, 1.45", SPI; touch = **CHSC6x** (I2C). Panel tech (LCD/AMOLED) + color depth unconfirmed — assume RGB565/16-bit. |
| **Input** | Wheel/**knob** (rotate + press), touchscreen, 1× RST button |
| **Indicator** | 1× RGB LED — **WS2812 addressable** on **GPIO40** (`BSP_RGB_CTRL`), driven via `led_strip`/RMT (`bsp_rgb_init`/`bsp_rgb_set`). Off in steady state (D31). |
| **Audio** | **Speaker + microphone** via an **ES8311 codec** (I2C `0x30`) over **I2S0** — MCLK GPIO10 / SCLK 11 / LRCK 12 / DIN 15 (mic) / DOUT 16 (spk); speaker power-amp on IO-expander `BSP_PWR_CODEC_PA` (in `BSP_PWR_START_UP`). BSP API: `bsp_audio_codec_speaker_init`, `bsp_audio_codec_microphone_init`, `bsp_codec_init`, `bsp_codec_volume_set`, `bsp_codec_mute_set`, `bsp_i2s_write`/`read`, `bsp_codec_speaker_get`. **Unused by this project until the planned "speak usage/battery" feature.** |
| **Connectivity** | WiFi 802.11 b/g/n **2.4 GHz only**; Bluetooth 5 (BLE) |
| **USB** | **Two USB-C** — rear = power only; **bottom = power + programming/flashing** |
| **Power** | 5 V DC; 3.7 V 400 mAh Li-ion backup battery |
| **Expansion** | Grove I2C, 2×4 header, microSD (≤32 GB) |

## 2. Connection / Ports (this machine)
- Bottom USB-C exposes a **WCH CH342 dual USB-UART bridge** — `VID_1A86 & PID_55D2`.
  (Note: this is the CH342 bridge, **not** the ESP32-S3 native USB `303A`.)
- Enumerated here as:
  - **COM18** — `USB-Enhanced-SERIAL-B CH342` (interface MI_02) ← **CONFIRMED ESP32-S3 boot/flash UART** (esptool syncs here)
  - **COM19** — `USB-Enhanced-SERIAL-A CH342` (interface MI_00) — **no esptool response** (the other / Himax-side channel)
- WCH CH342 driver is installed (ports appear with status OK).
- ✅ **Confirmed 2026-06-24:** flash/boot port = **COM18**. Chip = ESP32-S3 rev v0.2, MAC `8c:bf:ea:09:b0:08`,
  32 MB flash, **flash-encryption & secure-boot DISABLED**. Use baud **460800** (921600 corrupts on this
  CH342 → "Invalid head of packet"). Auto-reset works (no manual BOOT button needed).

## 3. Toolchain Setup — **ESP-IDF** (not PlatformIO/Arduino)
The Watcher firmware is a **native ESP-IDF project** with custom board support (round display,
CHSC6x touch, knob, LVGL via `esp_lvgl_port`). There is **no Arduino core / no PlatformIO project**
for it, so ESP-IDF is the supported path. (Decision D7.)

**Required:**
1. **Git** (with submodule support — the firmware repo uses submodules).
2. **ESP-IDF v5.2.1** targeting `esp32s3`. (Firmware README says 5.2.1; Seeed wiki references 5.1.x —
   prefer 5.2.1 unless the repo's CI says otherwise.) The IDF install bundles the **Xtensa toolchain,
   `idf.py`, `esptool`, CMake, Ninja, Python**.
3. **WCH CH342 USB driver** — already installed here (COM18/COM19 present).

**Install options (Windows):**
- **A — Official ESP-IDF Windows Installer** (simplest; sets up everything + an "ESP-IDF CMD/PowerShell").
- **B — VS Code + Espressif IDF extension** (good DX; manages IDF versions + build/flash buttons).
- **C — Manual** (`git clone esp-idf`, `install.bat esp32s3`, `export.bat`).

## 4. Build & Flash Procedure
```sh
# 1. Get the firmware (with submodules)
git clone --recursive https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware
cd SenseCAP-Watcher-Firmware
git submodule update --init --recursive

# 2. Pick an example/app (start small), set target, build
idf.py set-target esp32s3
idf.py build

# 3. Flash + monitor over the bottom USB-C (try COM19 first)
idf.py -p COM19 flash monitor
```
- If flashing fails, enter **download/boot mode** (boot button held during reset) and retry; adjust baud if needed.
- Prebuilt binaries can also be written with `esptool`.

## 5. Starting Point (for our firmware)
- Build from **`examples/helloworld`** or **`examples/lvgl_demo`** (and `lvgl_encoder_demo` for knob nav)
  rather than gutting the full `factory_firmware`.
- Useful references in-repo: `examples/factory_firmware/main/view/` (LVGL screens),
  `components/` (display/touch/knob drivers, `esp_lvgl_port`).

## 6. Recovery / Restore Stock Firmware
- The custom firmware **replaces** the stock SenseCraft AI app while running — **reversible**.
- Restore by reflashing Seeed's **factory firmware** (prebuilt binary via `esptool`, or through the
  SenseCraft flashing flow). Document the exact factory-image URL/version when first restored.
- **Local recovery copy already on disk** (from the OSHW repo): ESP32 factory firmware at
  `references/OSHW-SenseCAP-Watcher/Firmware/ESP32/V1.1.7/` and Himax AI image at
  `references/OSHW-SenseCAP-Watcher/Firmware/Himax/himax_firmware_20240816.img`. Verify these against the
  latest Seeed release before relying on them for recovery.
  - ⚠️ This public image covers `bootloader/partition-table/ota_data/factory_firmware/srmodels/storage`
    **only** — it does **NOT** contain the per-device credentials at `0x9000`. See §6.1.

### 6.1 ⚠️ MANDATORY pre-flash backup (device identity) — Decision D9
The device holds **unique, per-unit secrets** that are **NOT** in any public factory image, so they are
**unrecoverable if erased**: `SN`, `EUI`/`BASICID`, `DEVICE_KEY`, `ACCESS_KEY`, `AES_KEY`, `DEV_CTL_KEY`
(visible on the boot console — see OSHW `assets/production_information.png`). They live in the
**`nvsfactory`** partition at **`0x9000`, size `0x32000` (200 KB)**.

Decoded factory partition map (V1.1.7):

    nvsfactory data,nvs   0x9000    200K   <- UNIQUE CREDENTIALS (back this up!)
    nvs        data,nvs   0x3b000   840K
    otadata    data,ota   0x10d000  8K
    phy_init   data,phy   0x10f000  4K
    ota_0      app        0x110000  12M
    ota_1      app        0xd10000  12M
    model      data,spiffs 0x1910000 1M
    storage    data,spiffs 0x1a10000 6080K

Why our custom firmware threatens it: the example partition tables (e.g. `helloworld`) relabel `0x9000`
as a generic `nvs`, so the app will likely **format/overwrite** the credentials on first boot; an
`erase-flash` wipes them outright. (`idf.py flash` alone doesn't write `0x9000`, but running the new app
does.)

**Run BEFORE any flash** (read-only, non-destructive; activate IDF env first; try COM19, else COM18):

    # full 32 MB image (gold-standard restore)
    esptool.py --chip esp32s3 -p COM19 -b 921600 read_flash 0x0 0x2000000 watcher_full_32MB.bin
    # just the credentials partition (matches Seeed README)
    esptool.py --chip esp32s3 -p COM19 -b 921600 read_flash 0x9000 0x32000 nvsfactory.bin
    # record eFuses (MAC etc.)
    espefuse.py --chip esp32s3 -p COM19 summary > efuse_summary.txt

Store these **outside the repo** — they contain live secrets, never commit them. Restore the credentials
later with `write_flash 0x9000 nvsfactory.bin`.

**✅ Backup completed & verified 2026-06-24** → `C:\esp\watcher_backup\` (outside repo; **contains live secrets**):
- `nvsfactory.bin` — 204,800 B — SHA256 `451091dafc4ff136f30ae73514d623240c8a167a80c61433a4136d4893c0c974`
- `watcher_full_32MB.bin` — 33,554,432 B (exact) — SHA256 `70872ff0fe8c719378518e912f096cbc51dce652d7cff2bb8d3e84f47d82e763`
- `efuse_summary.txt` — 17,559 B (MAC/fuses)

Verified: full image is exactly 32 MB and its `0x9000` slice byte-for-byte matches `nvsfactory.bin`.
Device: ESP32-S3 rev v0.2, MAC `8c:bf:ea:09:b0:08`. **Restore** (port **COM18** @ **460800**):

    # credentials only
    esptool.py --chip esp32s3 -p COM18 -b 460800 write_flash 0x9000 nvsfactory.bin
    # whole device (bit-for-bit)
    esptool.py --chip esp32s3 -p COM18 -b 460800 write_flash 0x0 watcher_full_32MB.bin

## 7. References
- Firmware/SDK: https://github.com/Seeed-Studio/SenseCAP-Watcher-Firmware
- Open hardware (schematic/PCB): https://github.com/Seeed-Studio/OSHW-SenseCAP-Watcher
- Hardware overview: https://wiki.seeedstudio.com/watcher_hardware_overview/
- Software framework: https://wiki.seeedstudio.com/watcher_software_framework/
- ESP-IDF: https://docs.espressif.com/projects/esp-idf/en/v5.2.1/esp32s3/
