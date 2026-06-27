#!/usr/bin/env python3
"""Embed the spoken-alert WAVs (16 kHz/16-bit/mono PCM) into main/speak_clips.c/.h
as int16 C arrays for playback via the BSP speaker (bsp_i2s_write).

   python wav_to_c.py

Trims leading/trailing silence (SAPI pads both ends). 10 separate arrays + an
index table; enum order = CLIPS below (must match speak.c's clip ids). Stdlib only.
"""
import wave, array, os, math

HERE   = os.path.dirname(os.path.abspath(__file__))
OUT_C  = os.path.join(HERE, "..", "..", "main", "speak_clips.c")
OUT_H  = os.path.join(HERE, "..", "..", "main", "speak_clips.h")
CLIPS  = ["usage_25", "usage_50", "usage_75", "usage_90", "usage_100",
          "batt_50", "batt_30", "batt_20", "batt_10", "batt_5"]
SILENCE = 350     # |amplitude| below this = silence (of 32767)
PAD     = 800     # keep ~50 ms (@16k) either side of speech

DRIVE = 1.8   # loudness: peak-normalize then soft-limit (tanh). >1 = louder, mild saturation.

def boost(a):
    """Make the clip as loud as possible without harsh clipping: peak-normalize to
    near full-scale, then push through a tanh soft-limiter (raises RMS/perceived
    loudness, peaks saturate smoothly instead of hard-clipping)."""
    peak = max(1, max(abs(x) for x in a))
    g = (0.97 * 32767.0 / peak) * DRIVE
    out = array.array("h")
    for x in a:
        y = 32767.0 * math.tanh(x * g / 32767.0)
        out.append(int(max(-32768, min(32767, round(y)))))
    return out

def load(name):
    w = wave.open(os.path.join(HERE, name + ".wav"), "rb")
    assert w.getnchannels() == 1 and w.getsampwidth() == 2, name + ": want mono/16-bit"
    rate = w.getframerate()
    a = array.array("h"); a.frombytes(w.readframes(w.getnframes()))
    w.close()
    lo, hi = 0, len(a) - 1
    while lo < hi and abs(a[lo]) < SILENCE: lo += 1
    while hi > lo and abs(a[hi]) < SILENCE: hi -= 1
    lo = max(0, lo - PAD); hi = min(len(a) - 1, hi + PAD)
    return rate, boost(a[lo:hi + 1])

clips = [(c, *load(c)) for c in CLIPS]
rate0 = clips[0][1]
assert all(r == rate0 for _, r, _ in clips), "all clips must share a sample rate"

with open(OUT_C, "w") as f:
    f.write("/* Auto-generated spoken-alert PCM (Zira, 16 kHz/16-bit mono) — DO NOT hand-edit.\n"
            " * Regenerate: tools/audio/gen_clips.ps1 (SAPI) then tools/audio/wav_to_c.py. */\n")
    f.write('#include "speak_clips.h"\n\n')
    total = 0
    for name, _, a in clips:
        total += len(a)
        f.write(f"static const int16_t spk_{name}[{len(a)}] = {{\n    ")
        for i, s in enumerate(a):
            f.write(f"{s},")
            f.write("\n    " if (i + 1) % 20 == 0 else " ")
        f.write("\n};\n\n")
    f.write("const speak_clip_t SPEAK_CLIPS[SPK_COUNT] = {\n")
    for name, _, a in clips:
        f.write(f"    {{ spk_{name}, {len(a)} }},\n")
    f.write("};\n")

with open(OUT_H, "w") as f:
    f.write("#pragma once\n#include <stdint.h>\n#include <stddef.h>\n\n")
    f.write("typedef enum {\n")
    for name, _, _ in clips:
        f.write(f"    SPK_{name.upper()},\n")
    f.write("    SPK_COUNT\n} speak_clip_id_t;\n\n")
    f.write("typedef struct { const int16_t *pcm; size_t samples; } speak_clip_t;\n")
    f.write("extern const speak_clip_t SPEAK_CLIPS[SPK_COUNT];\n")
    f.write(f"#define SPEAK_SAMPLE_RATE {rate0}\n")

print(f"wrote {OUT_C} + {OUT_H}: {len(clips)} clips, {total} samples "
      f"(~{total*2//1024} KB) @ {rate0} Hz")
