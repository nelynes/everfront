// Label scans to EverFront labels: the same steps as tools/make_pack.py, in the browser.
//
//   scan (any size, RGBA) -> composited onto black, Lanczos down to the label well,
//   a light unsharp mask, then 36 colours by median cut refined with k-means, no dithering.
//
// Pixels where the scan is mostly transparent (its rounded corners) become 255, "no label here",
// so the cart's plastic shows through. Images are plain {w, h, data: Uint8ClampedArray RGBA}.

export const NCOL = 36;

// ---------------------------------------------------------------- resampling (as Pillow does it)

const lanczos = (x) => {
  if (x === 0) return 1;
  if (x <= -3 || x >= 3) return 0;
  const px = Math.PI * x;
  return (3 * Math.sin(px) * Math.sin(px / 3)) / (px * px);
};

// Pillow's coefficients for one axis: the filter widened by the scale when shrinking.
function coeffs(inSize, outSize) {
  const scale = inSize / outSize;
  const fscale = Math.max(scale, 1);
  const support = 3 * fscale;
  const out = [];
  for (let x = 0; x < outSize; x++) {
    const center = (x + 0.5) * scale;
    const xmin = Math.max(Math.trunc(center - support + 0.5), 0);
    const xmax = Math.min(Math.trunc(center + support + 0.5), inSize);
    const w = [];
    let total = 0;
    for (let i = xmin; i < xmax; i++) {
      const k = lanczos((i - center + 0.5) / fscale);
      w.push(k);
      total += k;
    }
    out.push({ xmin, w: w.map((k) => (total ? k / total : 0)) });
  }
  return out;
}

const clip8 = (v) => (v < 0 ? 0 : v > 255 ? 255 : Math.round(v));

// Resize `ch` interleaved channels, horizontally then vertically, rounding between passes.
export function resample(src, sw, sh, ch, dw, dh) {
  const cx = coeffs(sw, dw);
  const tmp = new Uint8ClampedArray(dw * sh * ch);
  for (let y = 0; y < sh; y++) {
    for (let x = 0; x < dw; x++) {
      const { xmin, w } = cx[x];
      for (let c = 0; c < ch; c++) {
        let s = 0;
        for (let k = 0; k < w.length; k++) s += src[((y * sw) + xmin + k) * ch + c] * w[k];
        tmp[(y * dw + x) * ch + c] = clip8(s);
      }
    }
  }
  const cy = coeffs(sh, dh);
  const out = new Uint8ClampedArray(dw * dh * ch);
  for (let y = 0; y < dh; y++) {
    const { xmin, w } = cy[y];
    for (let x = 0; x < dw; x++) {
      for (let c = 0; c < ch; c++) {
        let s = 0;
        for (let k = 0; k < w.length; k++) s += tmp[((xmin + k) * dw + x) * ch + c] * w[k];
        out[(y * dw + x) * ch + c] = clip8(s);
      }
    }
  }
  return out;
}

// ---------------------------------------------------------------- unsharp mask (Pillow's)

// Pillow's GaussianBlur(radius) is three box blurs per axis, each of this (fractional) radius.
function boxRadius(radius, passes) {
  const sigma2 = (radius * radius) / passes;
  const L = Math.sqrt(12 * sigma2 + 1);
  const l = Math.floor((L - 1) / 2);
  let a = (2 * l + 1) * (l * (l + 1) - 3 * sigma2);
  a /= 6 * (sigma2 - (l + 1) * (l + 1));
  return l + a;
}

function boxPass(line, n, r) {
  const whole = Math.floor(r);
  const frac = r - whole;
  const at = (i) => line[i < 0 ? 0 : i >= n ? n - 1 : i];
  const out = new Float64Array(n);
  for (let i = 0; i < n; i++) {
    let s = 0;
    for (let k = -whole; k <= whole; k++) s += at(i + k);
    s += frac * (at(i - whole - 1) + at(i + whole + 1));
    out[i] = s / (2 * r + 1);
  }
  return out;
}

function blur(src, w, h, ch, radius) {
  const r = boxRadius(radius, 3);
  const f = Float64Array.from(src);
  for (let c = 0; c < ch; c++) {
    for (let y = 0; y < h; y++) {
      let line = new Float64Array(w);
      for (let x = 0; x < w; x++) line[x] = f[(y * w + x) * ch + c];
      for (let p = 0; p < 3; p++) line = boxPass(line, w, r);
      for (let x = 0; x < w; x++) f[(y * w + x) * ch + c] = line[x];
    }
    for (let x = 0; x < w; x++) {
      let line = new Float64Array(h);
      for (let y = 0; y < h; y++) line[y] = f[(y * w + x) * ch + c];
      for (let p = 0; p < 3; p++) line = boxPass(line, h, r);
      for (let y = 0; y < h; y++) f[(y * w + x) * ch + c] = line[y];
    }
  }
  return f;
}

export function unsharp(src, w, h, ch, radius = 1, percent = 60, threshold = 2) {
  const b = blur(src, w, h, ch, radius);
  const out = new Uint8ClampedArray(src.length);
  for (let i = 0; i < src.length; i++) {
    const diff = src[i] - Math.round(b[i]);
    out[i] = Math.abs(diff) >= threshold ? clip8(src[i] + Math.trunc((diff * percent) / 100)) : src[i];
  }
  return out;
}

// ---------------------------------------------------------------- 36 colours, no dithering

const dist2 = (r, g, b, p) => (r - p[0]) ** 2 + (g - p[1]) ** 2 + (b - p[2]) ** 2;

// Median cut: keep splitting the box holding the most pixels (that can still be split) along its
// widest channel, at the median pixel. Each box's mean is a palette colour.
function medianCut(colours, n) {
  // colours: [{r, g, b, count}]
  const box = (cs) => {
    let count = 0;
    const lo = [255, 255, 255];
    const hi = [0, 0, 0];
    for (const c of cs) {
      count += c.count;
      const v = [c.r, c.g, c.b];
      for (let k = 0; k < 3; k++) {
        if (v[k] < lo[k]) lo[k] = v[k];
        if (v[k] > hi[k]) hi[k] = v[k];
      }
    }
    return { cs, count, lo, hi };
  };
  const boxes = [box(colours)];
  while (boxes.length < n) {
    let best = -1;
    for (let i = 0; i < boxes.length; i++) {
      if (boxes[i].cs.length < 2) continue;
      if (best < 0 || boxes[i].count > boxes[best].count) best = i;
    }
    if (best < 0) break;
    const b = boxes[best];
    const ranges = [0, 1, 2].map((k) => b.hi[k] - b.lo[k]);
    const axis = ranges.indexOf(Math.max(...ranges));
    const key = ['r', 'g', 'b'][axis];
    const cs = [...b.cs].sort((p, q) => p[key] - q[key]);
    let acc = 0;
    let cut = 1;
    for (let i = 0; i < cs.length - 1; i++) {
      acc += cs[i].count;
      if (acc >= b.count / 2) {
        cut = i + 1;
        break;
      }
      cut = i + 1;
    }
    boxes.splice(best, 1, box(cs.slice(0, cut)), box(cs.slice(cut)));
  }
  return boxes.map((b) => {
    let r = 0;
    let g = 0;
    let bl = 0;
    for (const c of b.cs) {
      r += c.r * c.count;
      g += c.g * c.count;
      bl += c.b * c.count;
    }
    return [Math.round(r / b.count), Math.round(g / b.count), Math.round(bl / b.count)];
  });
}

function nearest(r, g, b, pal) {
  let bi = 0;
  let bd = Infinity;
  for (let i = 0; i < pal.length; i++) {
    const d = dist2(r, g, b, pal[i]);
    if (d < bd) {
      bd = d;
      bi = i;
    }
  }
  return bi;
}

// Median cut refined with k-means: pixels move to their nearest colour, colours to their pixels'
// mean, until (almost) nothing moves.
export function quantize(rgb, n = NCOL) {
  const counts = new Map();
  for (let i = 0; i < rgb.length; i += 3) {
    const k = (rgb[i] << 16) | (rgb[i + 1] << 8) | rgb[i + 2];
    counts.set(k, (counts.get(k) ?? 0) + 1);
  }
  const colours = [...counts].map(([k, count]) => ({ r: k >> 16, g: (k >> 8) & 255, b: k & 255, count }));
  let pal = medianCut(colours, n);
  let assign = colours.map((c) => nearest(c.r, c.g, c.b, pal));
  for (let it = 0; it < 20; it++) {
    const sum = pal.map(() => [0, 0, 0, 0]);
    colours.forEach((c, i) => {
      const s = sum[assign[i]];
      s[0] += c.r * c.count;
      s[1] += c.g * c.count;
      s[2] += c.b * c.count;
      s[3] += c.count;
    });
    pal = pal.map((p, i) => (sum[i][3] ? [0, 1, 2].map((k) => Math.round(sum[i][k] / sum[i][3])) : p));
    let moved = 0;
    const next = colours.map((c, i) => {
      const j = nearest(c.r, c.g, c.b, pal);
      if (j !== assign[i]) moved += c.count;
      return j;
    });
    assign = next;
    if (moved <= 4) break;
  }
  const lookup = new Map();
  colours.forEach((c, i) => lookup.set((c.r << 16) | (c.g << 8) | c.b, assign[i]));
  const idx = new Uint8Array(rgb.length / 3);
  for (let i = 0, p = 0; i < rgb.length; i += 3, p++) {
    idx[p] = lookup.get((rgb[i] << 16) | (rgb[i + 1] << 8) | rgb[i + 2]);
  }
  while (pal.length < n) pal.push([0, 0, 0]);
  return { cols: pal, idx };
}

// ---------------------------------------------------------------- one label

// A scan (any size) to a label: {cols: 36 x [r,g,b], idx: lw*lh bytes, 255 = no label}.
export function makeLabel(img, lw, lh) {
  const { w, h, data } = img;
  const rgb = new Uint8ClampedArray(w * h * 3);
  const alpha = new Uint8ClampedArray(w * h);
  for (let i = 0; i < w * h; i++) {
    const a = data[i * 4 + 3];
    alpha[i] = a;
    // pasted onto black through its own alpha, as make_pack.py does
    for (let c = 0; c < 3; c++) rgb[i * 3 + c] = Math.round((data[i * 4 + c] * a) / 255);
  }
  const a = resample(alpha, w, h, 1, lw, lh);
  let small = resample(rgb, w, h, 3, lw, lh);
  small = unsharp(small, lw, lh, 3);
  const { cols, idx } = quantize(small, NCOL);
  for (let i = 0; i < idx.length; i++) if (a[i] <= 128) idx[i] = 255;
  return { cols, idx, lw, lh };
}

// A label back to RGBA, for previews. `bg` shows where the label is absent.
export function labelToRgba(label, bg = [60, 60, 66]) {
  const out = new Uint8ClampedArray(label.lw * label.lh * 4);
  for (let i = 0; i < label.idx.length; i++) {
    const v = label.idx[i];
    // as the GBA shows it: 5 bits a channel
    const c = v === 255 ? bg : label.cols[v].map((x) => (x >> 3) << 3);
    out.set([c[0], c[1], c[2], 255], i * 4);
  }
  return out;
}
