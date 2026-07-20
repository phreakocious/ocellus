// Golden: packRgb565 must match config.html's copy AND tools/test_slide_convert.py. Run: node tools/test_slide_convert.mjs
// NOTE: keep this function byte-identical to packRgb565 in config.html.
function packRgb565(rgba) {
  const out = new Uint8Array(240 * 240 * 2), R = 120;
  for (let y = 0; y < 240; y++) for (let x = 0; x < 240; x++) {
    const i = (y * 240 + x) * 4, o = (y * 240 + x) * 2;
    let r = rgba[i], g = rgba[i + 1], b = rgba[i + 2];
    const dx = x - 120 + 0.5, dy = y - 120 + 0.5;
    if (dx * dx + dy * dy > R * R) { r = g = b = 0; }        // round crop
    const v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    out[o] = v & 0xFF; out[o + 1] = v >> 8;                  // little-endian
  }
  return out;
}
const rgba = new Uint8ClampedArray(240 * 240 * 4);
const set = (x, y, r, g, b) => { const i = (y * 240 + x) * 4; rgba[i] = r; rgba[i+1] = g; rgba[i+2] = b; rgba[i+3] = 255; };
set(120, 120, 248, 0, 0); set(10, 120, 0, 252, 0);
const out = packRgb565(rgba), off = (x, y) => (y * 240 + x) * 2;
const eq = (o, a, b) => { if (out[o] !== a || out[o+1] !== b) throw new Error(`fail @${o}: ${out[o]},${out[o+1]} != ${a},${b}`); };
eq(off(120,120), 0x00, 0xF8); eq(off(10,120), 0xE0, 0x07); eq(off(0,0), 0x00, 0x00);
console.log("OK: packRgb565 golden");
