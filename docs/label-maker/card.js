// The SD card: a folder the browser was given, either as a directory handle (Chrome, Edge: read
// and write) or as a list of files from a folder picker (any browser: read only).

import { crc32 } from './pack.js';
import { SYSTEMS } from './systems.js';

// A card, folder names matched without regard to case, as FAT does.
export class Card {
  // handle: FileSystemDirectoryHandle, or null with files: File[] (webkitRelativePath)
  constructor(handle, files) {
    this.handle = handle;
    this.files = files;
    this.writable = !!handle;
  }

  // Entries of a folder ('' = root): [{name, file()}] for files only.
  async list(folder) {
    if (this.handle) {
      const dir = folder ? await this.#dir(folder) : this.handle;
      if (!dir) return [];
      const out = [];
      for await (const [name, h] of dir.entries()) if (h.kind === 'file') out.push({ name, file: () => h.getFile() });
      return out;
    }
    // The picker's paths start with the chosen folder's own name.
    const out = [];
    for (const f of this.files) {
      const parts = f.webkitRelativePath.split('/').slice(1);
      const inside = folder ? parts.length === 2 && parts[0].toLowerCase() === folder.toLowerCase() : parts.length === 1;
      if (inside) out.push({ name: parts.at(-1), file: async () => f });
    }
    return out;
  }

  async #dir(name, create = false) {
    for await (const [n, h] of this.handle.entries()) if (h.kind === 'directory' && n.toLowerCase() === name.toLowerCase()) return h;
    return create ? this.handle.getDirectoryHandle(name, { create: true }) : null;
  }

  async read(folder, name) {
    const hit = (await this.list(folder)).find((e) => e.name.toLowerCase() === name.toLowerCase());
    return hit ? new Uint8Array(await (await hit.file()).arrayBuffer()) : null;
  }

  async hasFolder(name) {
    if (this.handle) return !!(await this.#dir(name));
    return this.files.some((f) => f.webkitRelativePath.split('/')[1]?.toLowerCase() === name.toLowerCase() && f.webkitRelativePath.split('/').length > 2);
  }

  async write(folder, name, bytes) {
    const dir = await this.#dir(folder, true);
    const fh = await dir.getFileHandle(name, { create: true });
    const out = await fh.createWritable();
    await out.write(bytes);
    await out.close();
  }
}

// Every shelf's games: [{system, folder, name (file), stem, ext, db, file()}].
export async function findGames(card) {
  const games = [];
  for (const s of SYSTEMS) {
    const seen = new Set();
    for (const folder of s.folders) {
      for (const e of await card.list(folder)) {
        const ext = e.name.includes('.') ? e.name.split('.').pop().toLowerCase() : '';
        if (!(ext in s.exts) || e.name.startsWith('.')) continue;
        const k = e.name.toLowerCase();
        if (seen.has(k)) continue;
        seen.add(k);
        games.push({ system: s, folder, name: e.name, stem: e.name.slice(0, -ext.length - 1), ext, db: s.exts[ext], file: e.file });
      }
    }
  }
  return games;
}

// The rom's checksums: of the whole file, and for NES of the file without its header too.
// Remembered per file (name, size, date) in this browser, as reading a big card takes a while.
export async function checksums(game, onBytes) {
  const f = await game.file();
  const memo = `ef-crc:${game.system.id}/${game.name}:${f.size}:${f.lastModified}`;
  try {
    const hit = localStorage.getItem(memo);
    if (hit) return JSON.parse(hit);
  } catch {
    // storage blocked: just read the file
  }
  const skip = game.system.headerless ?? 0;
  let whole = 0;
  let bare = 0;
  let at = 0;
  const reader = f.stream().getReader();
  for (;;) {
    const { done, value } = await reader.read();
    if (done) break;
    whole = crc32(value, whole);
    if (skip) {
      const from = Math.max(0, skip - at);
      if (from < value.length) bare = crc32(from ? value.subarray(from) : value, bare);
    }
    at += value.length;
    onBytes?.(value.length);
  }
  const hex = (c) => c.toString(16).toUpperCase().padStart(8, '0');
  const out = skip ? [hex(whole), hex(bare)] : [hex(whole)];
  try {
    localStorage.setItem(memo, JSON.stringify(out));
  } catch {
    // full or blocked: fine
  }
  return out;
}

// A zip (stored, no compression) of {name: bytes}, for browsers that can't write to the card.
export function zip(files) {
  const enc = new TextEncoder();
  const parts = [];
  const central = [];
  let offset = 0;
  for (const [name, data] of Object.entries(files)) {
    const n = enc.encode(name);
    const crc = crc32(data);
    const local = new DataView(new ArrayBuffer(30));
    local.setUint32(0, 0x04034b50, true);
    local.setUint16(4, 20, true);
    local.setUint32(14, crc, true);
    local.setUint32(18, data.length, true);
    local.setUint32(22, data.length, true);
    local.setUint16(26, n.length, true);
    parts.push(new Uint8Array(local.buffer), n, data);
    const c = new DataView(new ArrayBuffer(46));
    c.setUint32(0, 0x02014b50, true);
    c.setUint16(4, 20, true);
    c.setUint16(6, 20, true);
    c.setUint32(16, crc, true);
    c.setUint32(20, data.length, true);
    c.setUint32(24, data.length, true);
    c.setUint16(28, n.length, true);
    c.setUint32(42, offset, true);
    central.push(new Uint8Array(c.buffer), n);
    offset += 30 + n.length + data.length;
  }
  const size = central.reduce((s, p) => s + p.length, 0);
  const end = new DataView(new ArrayBuffer(22));
  end.setUint32(0, 0x06054b50, true);
  end.setUint16(8, Object.keys(files).length, true);
  end.setUint16(10, Object.keys(files).length, true);
  end.setUint32(12, size, true);
  end.setUint32(16, offset, true);
  return new Blob([...parts, ...central, new Uint8Array(end.buffer)], { type: 'application/zip' });
}
