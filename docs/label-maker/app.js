// EverFront Label Maker: open an SD card, find a label for every game, write the label packs.

import { Card, findGames, checksums, zip } from './card.js';
import { makeLabel, labelToRgba } from './convert.js';
import { buildPack, readPack, keyName, keyTitle, title } from './pack.js';
import { PLUS_ART, SLOT_ART, loadIndex, artFile, loadDat, thumb, limiter } from './sources.js';
import { logoLabel } from './logo-label.js';
import { SYSTEMS } from './systems.js';

const $ = (id) => document.getElementById(id);
const el = (tag, props = {}, ...kids) => {
  const e = Object.assign(document.createElement(tag), props);
  e.append(...kids.filter((k) => k != null));
  return e;
};

// What each kind of label is called on the page.
const KINDS = {
  scan: 'Your scan',
  card: 'On your card',
  plus: 'slotPlus art',
  slot: 'Slot art',
  logo: 'Logo',
  none: 'No label',
};

const state = {
  card: null,
  games: [],
  scans: new Map(), // keyName / keyTitle -> File
  existing: new Map(), // pack file -> readPack()
  indexes: null,
  busy: false,
};

// ---------------------------------------------------------------- images

const work = limiter(2);
const canvas = (w, h) => Object.assign(document.createElement('canvas'), { width: w, height: h });

async function decode(bytes) {
  const bitmap = await createImageBitmap(new Blob([bytes]));
  const cv = canvas(bitmap.width, bitmap.height);
  const cx = cv.getContext('2d');
  cx.drawImage(bitmap, 0, 0);
  return { w: cv.width, h: cv.height, data: cx.getImageData(0, 0, cv.width, cv.height).data, bitmap };
}

// ---------------------------------------------------------------- a game's options

// The label options for a game, best first: each {kind, image(): Promise<rgba|null>} or, for
// labels already on the card, {kind: 'card', packs: Map(file -> label)}.
async function optionsFor(g) {
  const opts = [];
  // scans and roms matched with accents in one form (Mac-made names split them in two)
  const nfc = g.stem.normalize('NFC');
  const own = state.scans.get(keyName(nfc)) ?? state.scans.get(keyTitle(nfc));
  if (own) opts.push({ kind: 'scan', image: once(async () => decode(new Uint8Array(await own.arrayBuffer()))) });

  const onCard = new Map();
  for (const p of g.system.packs) {
    const pack = state.existing.get(p.file);
    const lbl = pack?.keys.get(keyName(g.stem)) ?? pack?.keys.get(keyTitle(g.stem));
    if (lbl) onCard.set(p.file, lbl);
  }
  if (onCard.size) opts.push({ kind: 'card', packs: onCard });

  const { plus, slot } = state.indexes;
  const inSet = (index, media) => {
    for (const c of g.crcs) if (index?.[c]?.[media]) return index[c][media];
    return null;
  };
  const plusLabel = inSet(plus, 'support-texture');
  if (plusLabel) opts.push({ kind: 'plus', image: once(async () => fetchImage(PLUS_ART, plusLabel)) });
  const slotLabel = inSet(slot, 'support-texture');
  if (slotLabel) opts.push({ kind: 'slot', image: once(async () => fetchImage(SLOT_ART, slotLabel)) });

  // A logo, from either art set or libretro, on a colour from libretro's box art.
  const plusLogo = inSet(plus, 'wheel');
  const slotLogo = inSet(slot, 'wheel');
  const named = g.dbName ?? g.stem;
  const logo = once(async () => {
    const bytes =
      (plusLogo && (await artFile(PLUS_ART, plusLogo))) ||
      (slotLogo && (await artFile(SLOT_ART, slotLogo))) ||
      (await thumb(g.db, 'Named_Logos', named));
    return bytes ? decode(bytes) : null;
  });
  opts.push({
    kind: 'logo',
    image: once(async () => {
      const lg = await logo();
      if (!lg) return null;
      const boxBytes = await thumb(g.db, 'Named_Boxarts', named).catch(() => null);
      const box = boxBytes ? await decode(boxBytes).catch(() => null) : null;
      return { logo: lg, box };
    }),
  });
  opts.push({ kind: 'none' });
  return opts;
}

const once = (f) => {
  let p;
  return () => (p ??= f().catch((e) => (console.warn(e), null)));
};

async function fetchImage(base, path) {
  const bytes = await artFile(base, path);
  return bytes ? decode(bytes) : null;
}

// The label an option gives for one pack, or null when it has none there.
async function labelFor(opt, pack, g) {
  if (opt.kind === 'none') return null;
  if (opt.kind === 'card') return opt.packs.get(pack.file) ?? null;
  const key = `${pack.w}x${pack.h}`;
  opt.labels ??= new Map();
  if (!opt.labels.has(key)) {
    opt.labels.set(
      key,
      (async () => {
        const src = await opt.image();
        if (!src) return null;
        return work(async () => {
          if (opt.kind === 'logo') {
            // composed four times larger than the label, then converted like any scan
            const big = logoLabel(src.logo, src.box, title(g.stem), pack.w * 4, pack.h * 4, canvas);
            return makeLabel(big, pack.w, pack.h);
          }
          return makeLabel(src, pack.w, pack.h);
        });
      })(),
    );
  }
  return opt.labels.get(key);
}

// The first option, from the chosen one on, that has a label for this pack.
async function resolve(g, pack) {
  for (let i = g.choice; i < g.options.length; i++) {
    const lbl = await labelFor(g.options[i], pack, g);
    if (lbl) return { label: lbl, kind: g.options[i].kind };
    if (g.options[i].kind === 'none') break;
  }
  return { label: null, kind: 'none' };
}

// ---------------------------------------------------------------- opening a card

async function open(card) {
  if (state.busy) return;
  state.busy = true;
  state.card = card;
  $('shelves').replaceChildren();
  $('write').disabled = true;
  $('result').hidden = true;
  try {
    step('Looking at your card…');
    if (!(await card.hasFolder('everfront')) && !(await card.hasFolder('edgba'))) {
      notice("This doesn't look like your EverDrive's SD card: it has no everfront or edgba folder. Choose the card itself, not a folder on it.", 'warn');
    }
    const games = await findGames(card);
    if (!games.length) {
      step('');
      notice('No games found. EverFront looks for them in folders such as "Game Boy Advance", "GBA", "NES" or "Game Gear" at the card\'s root.', 'warn');
      return;
    }
    state.games = games;

    state.existing.clear();
    for (const s of SYSTEMS) {
      if (!games.some((g) => g.system === s)) continue;
      for (const p of s.packs) {
        const bytes = await card.read('everfront', p.file).catch(() => null);
        const pack = bytes && readPack(bytes);
        if (pack && pack.lw === p.w && pack.lh === p.h) state.existing.set(p.file, pack);
      }
    }

    state.indexes ??= await Promise.all([loadIndex(PLUS_ART), loadIndex(SLOT_ART)]).then(([plus, slot]) => ({ plus, slot }));

    // Who each game is: by its file name when that's a No-Intro name of the right size (no need
    // to read the rom), otherwise by its checksum.
    const total = games.length;
    let done = 0;
    for (const g of games) {
      step(`Identifying games: ${done} of ${total}`, done / total);
      const dat = await loadDat(g.db);
      const f = await g.file();
      const crcs = new Set();
      if (dat.byName.has(g.stem) && dat.size.get(g.stem) === f.size) {
        g.dbName = g.stem;
      } else {
        for (const c of await checksums(g)) {
          crcs.add(c);
          if (!g.dbName && dat.byCrc.has(c)) g.dbName = dat.byCrc.get(c);
        }
      }
      for (const c of dat.byName.get(g.dbName) ?? []) crcs.add(c);
      g.crcs = [...crcs];
      done++;
    }

    for (const g of games) {
      g.options = await optionsFor(g);
      g.choice = 0;
    }
    render();
    step('Finding labels…', 0);
    let shown = 0;
    await Promise.all(
      games.map(async (g) => {
        await refresh(g);
        shown++;
        step(`Finding labels: ${shown} of ${games.length}`, shown / games.length);
      }),
    );
    step('');
    summary();
    $('write').disabled = false;
  } catch (e) {
    console.error(e);
    notice(`Something went wrong reading the card: ${e.message}`, 'error');
  } finally {
    state.busy = false;
  }
}

// ---------------------------------------------------------------- the page

function step(text, frac) {
  $('status').textContent = text;
  $('progress').hidden = frac == null;
  if (frac != null) $('progress').value = frac;
}

function notice(text, kind = 'info') {
  $('notices').append(el('p', { className: `notice ${kind}`, textContent: text }));
}

function render() {
  const shelves = $('shelves');
  shelves.replaceChildren();
  for (const s of SYSTEMS) {
    const games = state.games.filter((g) => g.system === s);
    if (!games.length) continue;
    const grid = el('div', { className: 'grid' });
    for (const g of games.sort((a, b) => a.stem.localeCompare(b.stem))) grid.append(tile(g));
    shelves.append(
      el('section', { className: 'shelf', id: `shelf-${s.id}` },
        el('h2', {}, s.name, el('span', { className: 'count', id: `count-${s.id}` }, `${games.length} games`)),
        grid),
    );
  }
}

function tile(g) {
  const pack = g.system.packs[0];
  const cv = canvas(pack.w, pack.h);
  cv.className = 'label';
  cv.style.aspectRatio = `${pack.w} / ${pack.h}`;
  const badge = el('span', { className: 'badge' }, '…');
  const next = el('button', { className: 'icon', title: 'Try the next label', type: 'button', textContent: '↻' });
  const mine = el('button', { className: 'icon', title: 'Use an image of your own', type: 'button', textContent: '＋' });
  const none = el('button', { className: 'icon', title: 'No label (a plain one with the name)', type: 'button', textContent: '✕' });
  const t = el('figure', { className: 'tile' },
    el('div', { className: 'cart' }, cv),
    el('figcaption', {}, el('span', { className: 'name', title: g.name, textContent: title(g.stem) }), badge),
    el('div', { className: 'actions' }, next, mine, none));
  g.view = { cv, badge, tile: t };
  next.onclick = () => {
    const start = g.choice;
    g.choice = (g.choice + 1) % g.options.length;
    if (g.options[g.choice].kind === 'none' && start !== g.options.length - 1) g.choice = (g.choice + 1) % g.options.length;
    refresh(g).then(summary);
  };
  none.onclick = () => {
    g.choice = g.options.findIndex((o) => o.kind === 'none');
    refresh(g).then(summary);
  };
  mine.onclick = () => {
    const pick = el('input', { type: 'file', accept: 'image/*' });
    pick.onchange = () => {
      const f = pick.files[0];
      if (!f) return;
      g.options = g.options.filter((o) => o.kind !== 'scan');
      g.options.unshift({ kind: 'scan', image: once(async () => decode(new Uint8Array(await f.arrayBuffer()))) });
      g.choice = 0;
      refresh(g).then(summary);
    };
    pick.click();
  };
  return t;
}

async function refresh(g) {
  const pack = g.system.packs[0];
  g.view.badge.textContent = '…';
  const { label, kind } = await resolve(g, pack);
  g.shown = kind;
  g.view.badge.textContent = KINDS[kind];
  g.view.badge.dataset.kind = kind;
  g.view.tile.classList.toggle('missing', !label);
  const cx = g.view.cv.getContext('2d');
  if (label) {
    cx.putImageData(new ImageData(labelToRgba(label, [0, 0, 0]), pack.w, pack.h), 0, 0);
  } else {
    // as EverFront shows a game with no label: a plain colour with its name
    cx.fillStyle = '#3a3f4b';
    cx.fillRect(0, 0, pack.w, pack.h);
  }
}

function summary() {
  for (const s of SYSTEMS) {
    const games = state.games.filter((g) => g.system === s);
    if (!games.length) continue;
    const have = games.filter((g) => g.shown && g.shown !== 'none').length;
    $(`count-${s.id}`).textContent = `${have} of ${games.length} labelled`;
  }
  const n = state.games.length;
  const have = state.games.filter((g) => g.shown && g.shown !== 'none').length;
  $('summary').textContent = `${have} of ${n} games have a label.`;
}

// ---------------------------------------------------------------- your own scans

async function addScans(files) {
  let n = 0;
  for (const f of files) {
    if (!/\.(png|jpe?g|webp)$/i.test(f.name)) continue;
    const name = f.name.replace(/\.[^.]*$/, '').normalize('NFC');
    state.scans.set(keyName(name), f);
    if (!state.scans.has(keyTitle(name))) state.scans.set(keyTitle(name), f);
    n++;
  }
  $('scans-note').textContent = n ? `${n} of your scans added. They're used before any other label.` : 'No images in that folder.';
  if (!state.games.length) return;
  for (const g of state.games) {
    g.options = await optionsFor(g);
    g.choice = 0;
  }
  await Promise.all(state.games.map(refresh));
  summary();
}

// ---------------------------------------------------------------- writing

async function write() {
  if (state.busy) return;
  state.busy = true;
  $('write').disabled = true;
  const out = {};
  try {
    const systems = SYSTEMS.filter((s) => state.games.some((g) => g.system === s));
    const packs = systems.flatMap((s) => s.packs.map((p) => ({ s, p })));
    let i = 0;
    for (const { s, p } of packs) {
      step(`Making ${p.file} (${s.name}, ${p.theme} theme)`, i / packs.length);
      const entries = [];
      for (const g of state.games.filter((x) => x.system === s)) {
        const { label } = await resolve(g, p);
        if (label) entries.push({ names: [g.stem], label });
      }
      // No labels and no pack there already: nothing to write (EverFront shows plain labels).
      // An existing pack is still replaced, so labels taken off really go.
      if (entries.length || (await state.card.read('everfront', p.file).catch(() => null))) {
        out[p.file] = buildPack(entries, p.w, p.h);
      }
      i++;
    }
    if (state.card.writable) {
      let k = 0;
      for (const [file, bytes] of Object.entries(out)) {
        step(`Writing ${file} to the card`, k++ / Object.keys(out).length);
        await state.card.write('everfront', file, bytes);
      }
      step('');
      done(`Done: ${Object.keys(out).length} label packs written to the everfront folder on your card. Put the card back in your EverDrive and start EverFront.`);
    } else {
      const files = Object.fromEntries(Object.entries(out).map(([f, b]) => [`everfront/${f}`, b]));
      const url = URL.createObjectURL(zip(files));
      el('a', { href: url, download: 'everfront-labels.zip' }).click();
      step('');
      done('Your browser can\'t write to the card directly, so your labels were downloaded as everfront-labels.zip. Unzip it and copy its everfront folder onto the root of your SD card, over the existing one.');
    }
  } catch (e) {
    console.error(e);
    notice(`Couldn't write the labels: ${e.message}`, 'error');
  } finally {
    state.busy = false;
    $('write').disabled = false;
  }
}

function done(text) {
  $('result').hidden = false;
  $('result').textContent = text;
  $('result').scrollIntoView({ behavior: 'smooth', block: 'center' });
}

// ---------------------------------------------------------------- start

const direct = 'showDirectoryPicker' in window;
$('open').hidden = !direct;
$('open-files-label').hidden = direct;
$('browser-note').hidden = direct;

$('open').onclick = async () => {
  let handle;
  try {
    handle = await window.showDirectoryPicker({ id: 'everfront-card', mode: 'readwrite' });
  } catch (e) {
    if (e.name !== 'AbortError') notice(e.message, 'error');
    return;
  }
  $('notices').replaceChildren();
  open(new Card(handle));
};
$('open-files').onchange = (e) => {
  const files = [...e.target.files];
  e.target.value = '';
  $('notices').replaceChildren();
  if (files.length) open(new Card(null, files));
};
$('scans').onchange = (e) => {
  const files = [...e.target.files];
  e.target.value = '';
  addScans(files);
};
$('write').onclick = write;

// For tests: open a card from a directory handle without the picker.
window.__everfrontOpen = (handle) => open(new Card(handle));
