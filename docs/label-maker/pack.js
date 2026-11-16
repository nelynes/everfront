// EverFront label packs ("EFLB" version 3), as tools/make_pack.py writes them.
//
//   "EFLB" u16 version(3) u16 label_w u16 label_h u16 ncolours u32 count
//   count x { u32 key, u32 offset }  sorted by key
//   at offset: ncolours x u16 BGR555, then label_w * label_h bytes (255 = no label)
//
// Keys, as the GBA looks them up (src/main.c, label_offset):
//   full name   (crc32(name without extension, A-Z lowercased) & 0x3FFFFFFF) | 0x80000000
//   title only  the same over the name up to the first " (" or " [",      | 0xC0000000

const TABLE = (() => {
  const t = new Uint32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c >>> 0;
  }
  return t;
})();

// crc32 over bytes, resumable: pass the previous result to continue.
export function crc32(bytes, crc = 0) {
  let c = ~crc >>> 0;
  for (let i = 0; i < bytes.length; i++) c = TABLE[(c ^ bytes[i]) & 255] ^ (c >>> 8);
  return ~c >>> 0;
}

const utf8 = new TextEncoder();

// Only A-Z are lowercased, as the GBA does, so a name with accents keys the same on both sides.
function lowerBytes(s) {
  const b = utf8.encode(s);
  for (let i = 0; i < b.length; i++) if (b[i] >= 65 && b[i] <= 90) b[i] += 32;
  return b;
}

export const stem = (file) => file.replace(/\.[^.]*$/, '');
export const title = (name) => {
  const m = name.search(/ [([]/);
  return m < 0 ? name : name.slice(0, m);
};
export const keyName = (name) => ((crc32(lowerBytes(name)) & 0x3fffffff) | 0x80000000) >>> 0;
export const keyTitle = (name) => ((crc32(lowerBytes(title(name))) & 0x3fffffff) | 0xc0000000) >>> 0;

const bgr555 = ([r, g, b]) => (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10);

// entries: [{ names: [rom names, no extension], label: {cols, idx} }]. Each rom is keyed by its
// full name; its title too, unless another rom already claimed that title.
export function buildPack(entries, lw, lh, ncol = 36) {
  const keys = new Map();
  const weak = new Map();
  entries.forEach((e, bi) => {
    for (const n of e.names) {
      keys.set(keyName(n), bi);
      if (!weak.has(keyTitle(n))) weak.set(keyTitle(n), bi);
    }
  });
  for (const [k, bi] of weak) if (!keys.has(k)) keys.set(k, bi);
  const order = [...keys.keys()].sort((a, b) => a - b);
  const head = 16 + 8 * order.length;
  const blob = 2 * ncol + lw * lh;
  const out = new Uint8Array(head + blob * entries.length);
  const dv = new DataView(out.buffer);
  out.set([0x45, 0x46, 0x4c, 0x42], 0); // EFLB
  dv.setUint16(4, 3, true);
  dv.setUint16(6, lw, true);
  dv.setUint16(8, lh, true);
  dv.setUint16(10, ncol, true);
  dv.setUint32(12, order.length, true);
  order.forEach((k, i) => {
    dv.setUint32(16 + 8 * i, k, true);
    dv.setUint32(20 + 8 * i, head + keys.get(k) * blob, true);
  });
  entries.forEach((e, bi) => {
    const at = head + bi * blob;
    for (let c = 0; c < ncol; c++) dv.setUint16(at + 2 * c, bgr555(e.label.cols[c] ?? [0, 0, 0]), true);
    out.set(e.label.idx, at + 2 * ncol);
  });
  return out;
}

// A pack back to {lw, lh, ncol, keys: Map(key -> label)}, to show what a card already has.
export function readPack(bytes) {
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (String.fromCharCode(...bytes.subarray(0, 4)) !== 'EFLB' || dv.getUint16(4, true) !== 3) return null;
  const lw = dv.getUint16(6, true);
  const lh = dv.getUint16(8, true);
  const ncol = dv.getUint16(10, true);
  const n = dv.getUint32(12, true);
  const keys = new Map();
  for (let i = 0; i < n; i++) {
    const off = dv.getUint32(20 + 8 * i, true);
    const cols = [];
    for (let c = 0; c < ncol; c++) {
      const v = dv.getUint16(off + 2 * c, true);
      cols.push([(v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3]);
    }
    keys.set(dv.getUint32(16 + 8 * i, true), { cols, idx: bytes.slice(off + 2 * ncol, off + 2 * ncol + lw * lh), lw, lh });
  }
  return { lw, lh, ncol, keys };
}
