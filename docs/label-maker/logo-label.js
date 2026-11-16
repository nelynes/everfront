// A label for a game with no scan: its logo on a two-stop gradient, coloured from its box art.
// Ported from Cart Studio+ (github.com/nelynes/slotPlus-studio, src/label.rs and src/hue.rs,
// GPL-3.0-or-later), so these carts look like the ones it makes for slotPlus.

function hslToRgb(h, s, l) {
  const c = (1 - Math.abs(2 * l - 1)) * s;
  const sector = (((h % 360) + 360) % 360) / 60;
  const x = c * (1 - Math.abs((sector % 2) - 1));
  const [r, g, b] = [[c, x, 0], [x, c, 0], [0, c, x], [0, x, c], [x, 0, c], [c, 0, x]][Math.min(5, Math.floor(sector))];
  const m = l - c / 2;
  return [r, g, b].map((v) => Math.max(0, Math.min(255, Math.round((v + m) * 255))));
}

function rgbToHsv(r, g, b) {
  [r, g, b] = [r / 255, g / 255, b / 255];
  const max = Math.max(r, g, b);
  const d = max - Math.min(r, g, b);
  let hue = 0;
  if (d !== 0) {
    if (max === r) hue = 60 * ((((g - b) / d) % 6) + 6) % 360;
    else if (max === g) hue = 60 * ((b - r) / d + 2);
    else hue = 60 * ((r - g) / d + 4);
  }
  return [hue, max === 0 ? 0 : d / max, max];
}

// The hue for a cart with no box art: FNV-1a over its title, as slot colours its plain labels.
export function fallbackHue(title) {
  let h = 0xcbf29ce484222325n;
  for (const b of new TextEncoder().encode(title)) {
    h ^= BigInt(b);
    h = (h * 0x100000001b3n) & 0xffffffffffffffffn;
  }
  return Number(h % 360n);
}

// The box art's dominant saturated hue, or null: edges cropped, dull and dark pixels ignored.
export function boxHue({ w, h, data }) {
  const mx = Math.floor(w * 0.06);
  const my = Math.floor(h * 0.06);
  const bins = new Float64Array(36);
  for (let y = my; y < h - my; y++) {
    for (let x = mx; x < w - mx; x++) {
      const i = (y * w + x) * 4;
      if (data[i + 3] < 128) continue;
      const [hue, s, v] = rgbToHsv(data[i], data[i + 1], data[i + 2]);
      if (s < 0.35 || v < 0.25) continue;
      bins[Math.min(35, Math.floor(hue / 10))] += s * v;
    }
  }
  let best = 0;
  for (let i = 1; i < 36; i++) if (bins[i] > bins[best]) best = i;
  return bins[best] > 0 ? best * 10 + 5 : null;
}

// Mean luminance of the logo's opaque pixels: a bright logo gets a deep ground, a dark one a pale.
export function logoLuma({ w, h, data }) {
  let sum = 0;
  let n = 0;
  for (let i = 0; i < w * h; i++) {
    if (data[i * 4 + 3] < 200) continue;
    sum += 0.2126 * data[i * 4] + 0.7152 * data[i * 4 + 1] + 0.0722 * data[i * 4 + 2];
    n++;
  }
  return n ? sum / n : 255;
}

const BRIGHT = 110;
export function stops(hue, luma) {
  if (luma < BRIGHT) return [hslToRgb(hue, 0.55, 0.62), hslToRgb(hue, 0.5, 0.84)];
  const h = hue >= 45 && hue <= 75 ? 38 : hue; // yellow has no deep form: amber instead of olive
  return [hslToRgb(h, 0.65, 0.24), hslToRgb(h, 0.6, 0.46)];
}

function trim({ w, h, data }) {
  let x0 = w;
  let y0 = h;
  let x1 = 0;
  let y1 = 0;
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      if (data[(y * w + x) * 4 + 3] < 16) continue;
      if (x < x0) x0 = x;
      if (y < y0) y0 = y;
      if (x + 1 > x1) x1 = x + 1;
      if (y + 1 > y1) y1 = y + 1;
    }
  }
  return x1 > x0 ? [x0, y0, x1, y1] : null;
}

// The logo's box on the label, as fractions of slot's 1280x640 GBA label.
const BOX_W_OF = 920 / 1280;
const BOX_H_OF = 423 / 640;
const TALL_TOP_OF = 70 / 640;
const MID_ROW_OF = 0.5;

// The label as RGBA at w x h (compose it a few times larger than the pack's label, then convert).
// logo: {w, h, data, bitmap}, where bitmap is something canvas can draw. box: the box art, or null.
export function logoLabel(logo, box, title, w, h, makeCanvas) {
  const hue = (box && boxHue(box)) ?? fallbackHue(title);
  const [deep, pale] = stops(hue, logoLuma(logo));
  const cv = makeCanvas(w, h);
  const cx = cv.getContext('2d');
  const img = cx.createImageData(w, h);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const t = (0.88 * x) / w + (0.12 * y) / h;
      const i = (y * w + x) * 4;
      for (let c = 0; c < 3; c++) img.data[i + c] = Math.round(deep[c] + (pale[c] - deep[c]) * t);
      img.data[i + 3] = 255;
    }
  }
  cx.putImageData(img, 0, 0);
  const bounds = trim(logo);
  if (bounds) {
    const [x0, y0, x1, y1] = bounds;
    const lw = x1 - x0;
    const lh = y1 - y0;
    let scale;
    let dx;
    let dy;
    if (h > w) {
      // A tall label (NES): the logo across the top, as the real ones carry the title there.
      scale = Math.min((0.88 * w) / lw, (0.3 * h) / lh);
      dx = (w - lw * scale) / 2;
      dy = 0.2 * h - (lh * scale) / 2;
    } else {
      const byW = (BOX_W_OF * w) / lw;
      const byH = (BOX_H_OF * h) / lh;
      scale = Math.min(byW, byH);
      dx = (w - lw * scale) / 2;
      dy = byH <= byW ? TALL_TOP_OF * h : MID_ROW_OF * h - (lh * scale) / 2;
    }
    cx.imageSmoothingQuality = 'high';
    cx.drawImage(logo.bitmap, x0, y0, lw, lh, dx, dy, lw * scale, lh * scale);
  }
  return { w, h, data: cx.getImageData(0, 0, w, h).data };
}
