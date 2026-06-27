/* Regenerate main/icons_lucide.c — Lucide (MIT) SVG -> sharp raster -> alpha-8 C arrays
 * for LVGL 8.4 (recolored at runtime via lv_obj_set_style_img_recolor).
 *
 *   cd tools/icontool && npm install && node gen_icons.mjs
 *
 * Per-icon sizes follow 07-UI-REDESIGN D28: top-bar wifi/battery enlarged, the
 * SERVICE health glyph enlarged most (it is the screen's headline symbol), zap a
 * small fixed accent. Edit the ICONS table to change sizes, then re-run + rebuild.
 */
import sharp from 'sharp';
import { readFileSync, writeFileSync, existsSync } from 'fs';
import { fileURLToPath } from 'url';
import path from 'path';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const ICON_DIR = path.join(HERE, 'node_modules', 'lucide-static', 'icons');
const OUT = path.join(HERE, '..', '..', 'main', 'icons_lucide.c');

/* C symbol -> [lucide-static svg name, target px] */
const ICONS = [
  ['ic_wifi',         'wifi',             24],
  ['ic_wifi_off',     'wifi-off',         24],
  ['ic_batt',         'battery',          24],
  ['ic_batt_low',     'battery-low',      24],
  ['ic_batt_med',     'battery-medium',   24],
  ['ic_batt_full',    'battery-full',     24],
  ['ic_batt_chg',     'battery-charging', 24],
  ['ic_health_ok',    'circle-check',     30],
  ['ic_health_warn',  'triangle-alert',   30],
  ['ic_health_err',   'circle-x',         30],
  ['ic_health_maint', 'wrench',           30],
  ['ic_zap',          'zap',              24],
];

function svgFile(name) {
  const p = path.join(ICON_DIR, name + '.svg');
  if (!existsSync(p)) throw new Error(`missing lucide icon "${name}" at ${p}`);
  return p;
}

async function rasterAlpha(name, size) {
  const svg = readFileSync(svgFile(name));
  /* Lucide = stroke="currentColor" on transparent; sharp renders strokes opaque
   * black, so the alpha plane IS the glyph mask. Raster at high density then fit. */
  const { data, info } = await sharp(svg, { density: 512 })
    .resize(size, size, { fit: 'contain', background: { r: 0, g: 0, b: 0, alpha: 0 } })
    .ensureAlpha()
    .raw()
    .toBuffer({ resolveWithObject: true });
  const n = info.width * info.height;
  const a = Buffer.alloc(n);
  for (let i = 0; i < n; i++) a[i] = data[i * 4 + 3];   /* alpha channel */
  return { a, w: info.width, h: info.height };
}

function emit(sym, a, w, h) {
  let s = `static const uint8_t ${sym}_map[${w * h}] = {\n    `;
  for (let i = 0; i < a.length; i++) {
    s += '0x' + a[i].toString(16).padStart(2, '0') + ',';
    s += ((i + 1) % 20 === 0) ? '\n    ' : ' ';
  }
  s += `\n};\nconst lv_img_dsc_t ${sym} = {\n` +
       `    .header.cf = LV_IMG_CF_ALPHA_8BIT,\n` +
       `    .header.always_zero = 0,\n` +
       `    .header.w = ${w},\n` +
       `    .header.h = ${h},\n` +
       `    .data_size = ${w * h},\n` +
       `    .data = ${sym}_map,\n};\n\n`;
  return s;
}

let out =
`/* Auto-generated Lucide icons (alpha-8bit) for LVGL 8.4 — DO NOT hand-edit.
 * Source: lucide-static (MIT). Regenerate: tools/icontool/gen_icons.mjs (npm i && node gen_icons.mjs).
 * Per-icon sizes: top-bar 24, health 30, zap 24 (P2 enlargement reverted per user feedback 2026-06-27).
 * Monochrome alpha plane; set color at runtime via lv_obj_set_style_img_recolor(). */
#include "lvgl.h"

`;

for (const [sym, name, size] of ICONS) {
  const { a, w, h } = await rasterAlpha(name, size);
  out += emit(sym, a, w, h);
  console.error(`  ${sym.padEnd(16)} <- ${name.padEnd(18)} ${w}x${h}`);
}
writeFileSync(OUT, out);
console.error(`wrote ${OUT}`);
