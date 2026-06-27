#!/usr/bin/env python3
"""Option B host decoder: reset the Watcher, read the @@SHOT/@@END RLE-hex frames
it streams on boot (ui_screenshot via lv_snapshot), and write one PNG per screen.

No MCP, no simulator — ground-truth pixels off the real device. PNG is written
with the stdlib (zlib) so only pyserial is required (present in the ESP-IDF venv).

Usage:  python grab_screens.py [COM18] [out_dir] [timeout_s]
"""
import sys, time, re, zlib, struct
import serial   # pyserial (ships with the ESP-IDF python venv)

PORT     = sys.argv[1] if len(sys.argv) > 1 else "COM18"
OUT_DIR  = sys.argv[2] if len(sys.argv) > 2 else "."
TIMEOUT  = float(sys.argv[3]) if len(sys.argv) > 3 else 90.0
BAUD     = 115200

HEXSET = set("0123456789abcdefABCDEF")


def write_png(path, w, h, rgb):
    """Minimal RGB PNG via stdlib zlib (no PIL)."""
    def chunk(typ, data):
        return (struct.pack(">I", len(data)) + typ + data +
                struct.pack(">I", zlib.crc32(typ + data) & 0xffffffff))
    raw = bytearray()
    for y in range(h):
        raw.append(0)                       # filter byte 0 (none) per scanline
        raw += rgb[y * w * 3:(y + 1) * w * 3]
    png  = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def decode(w, h, hexstr):
    hexstr = "".join(c for c in hexstr if c in HEXSET)
    rgb = bytearray()
    npix = 0
    for i in range(0, len(hexstr) - 7, 8):
        cnt = int(hexstr[i:i + 4], 16)
        val = int(hexstr[i + 4:i + 8], 16)
        sw  = ((val & 0xFF) << 8) | (val >> 8)      # un-swap LV_COLOR_16_SWAP
        r = (sw >> 11) & 0x1F
        g = (sw >> 5) & 0x3F
        b = sw & 0x1F
        rgb += bytes([(r * 255) // 31, (g * 255) // 63, (b * 255) // 31]) * cnt
        npix += cnt
    need = w * h * 3
    if len(rgb) < need:
        rgb += bytes(need - len(rgb))
    return bytes(rgb[:need]), npix


def main():
    ser = serial.Serial(PORT, BAUD, timeout=2)
    ser.dtr = False
    ser.rts = True; time.sleep(0.2); ser.rts = False   # pulse reset -> fresh boot
    print(f"[grab] reset {PORT}, waiting for frames (<= {TIMEOUT:.0f}s)...")

    frames, state, name, w, h, hexbuf = {}, None, None, 0, 0, []
    deadline = time.time() + TIMEOUT
    while time.time() < deadline and len(frames) < 5:   # 5H / 7D / SERVICE / CLAWD / SETTINGS
        line = ser.readline()
        if not line:
            continue
        s = line.decode("ascii", "ignore").strip()
        if s.startswith("@@SHOT"):
            p = s.split()
            name, w, h, hexbuf, state = p[1], int(p[2]), int(p[3]), [], "read"
            print(f"[grab] frame {name} {w}x{h} ...")
        elif s.startswith("@@END"):
            if state == "read" and name:
                frames[name] = (w, h, "".join(hexbuf))
            state, name = None, None
        elif state == "read" and s and all(c in HEXSET for c in s):
            hexbuf.append(s)
    ser.close()

    if not frames:
        print("[grab] NO frames captured (timeout). Is the new firmware flashed?")
        sys.exit(1)

    for nm, (w, h, hx) in frames.items():
        rgb, npix = decode(w, h, hx)
        out = f"{OUT_DIR}/shot_{nm}.png"
        write_png(out, w, h, rgb)
        print(f"[grab] wrote {out}  ({npix} px decoded)")


if __name__ == "__main__":
    main()
