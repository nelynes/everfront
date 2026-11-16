// Where labels come from: two art sets keyed by rom checksum, and libretro's databases and
// thumbnails for names, logos and box art. Every base can be pointed elsewhere with a query
// parameter (?plusart= ?art= ?dat= ?thumbs=), which the tests use.

const q = new URLSearchParams(globalThis.location?.search ?? '');

// The slotPlus art set (same host as this page) and slot's own, as Cart Studio+ reads them:
// <platform>/<media>/<CRC32>.png with an index.json of checksum -> files.
export const PLUS_ART = q.get('plusart') ?? 'https://nelynes.github.io/slotPlus-art/';
export const SLOT_ART = q.get('art') ?? 'https://art.slot-cfw.fyi/';
// raw.githubusercontent.com answers cross-origin requests; libretro's own thumbnail host doesn't.
export const DAT = q.get('dat') ?? 'https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/';
export const THUMBS = q.get('thumbs') ?? 'https://raw.githubusercontent.com/libretro-thumbnails/';

// GitHub is not a CDN: a card can ask for hundreds of images, a few at a time.
export function limiter(slots) {
  let running = 0;
  const waiting = [];
  const pump = () => {
    while (running < slots && waiting.length) {
      const { job, resolve, reject } = waiting.shift();
      running++;
      job()
        .then(resolve, reject)
        .finally(() => {
          running--;
          pump();
        });
    }
  };
  return (job) =>
    new Promise((resolve, reject) => {
      waiting.push({ job, resolve, reject });
      pump();
    });
}
const net = limiter(6);

async function get(url) {
  return net(async () => {
    const r = await fetch(url);
    if (r.status === 404) return null;
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return new Uint8Array(await r.arrayBuffer());
  });
}

// An art set's index, or null when it can't be read (the page carries on without it).
export async function loadIndex(base) {
  try {
    const r = await fetch(new URL('index.json', base));
    if (!r.ok) throw new Error(`HTTP ${r.status}`);
    return await r.json();
  } catch (e) {
    console.warn(`art set ${base}: ${e.message}`);
    return null;
  }
}

export const artFile = (base, path) => get(new URL(path, base));

// A No-Intro database: each dump's checksum to its game, and each game to all its checksums.
export function parseDat(text) {
  const byCrc = new Map();
  const byName = new Map();
  const size = new Map();
  for (const [, body] of text.matchAll(/game \(\n([\s\S]*?)\n\)/g)) {
    const name = body.match(/^\s*name "([^"]*)"/m)?.[1];
    if (!name) continue;
    for (const [, file, bytes, crc] of body.matchAll(/rom \( name "([^"]*)" size (\d+) crc ([0-9A-Fa-f]{8})/g)) {
      const c = crc.toUpperCase();
      byCrc.set(c, name);
      if (!byName.has(name)) byName.set(name, []);
      byName.get(name).push(c);
      if (!/\.unh$/i.test(file)) size.set(name, Number(bytes));
    }
  }
  return { byCrc, byName, size };
}

const dats = new Map();
export function loadDat(db) {
  if (!dats.has(db)) {
    dats.set(
      db,
      fetch(`${DAT}${encodeURIComponent(db)}.dat`)
        .then((r) => (r.ok ? r.text() : Promise.reject(new Error(`HTTP ${r.status}`))))
        .then(parseDat)
        .catch((e) => {
          console.warn(`${db}: ${e.message}`);
          return { byCrc: new Map(), byName: new Map(), size: new Map() };
        }),
    );
  }
  return dats.get(db);
}

// libretro's thumbnail file name for a game: these characters become underscores.
export const thumbName = (name) => name.replace(/[&*/:`<>?\\|"]/g, '_');

// A libretro thumbnail ('Named_Logos', 'Named_Boxarts'), or null. A git symlink comes back as a
// tiny text body naming its target in the same folder.
export async function thumb(db, folder, name) {
  const base = `${THUMBS}${db.replaceAll(' ', '_')}/master/${folder}/`;
  const body = await get(base + encodeURIComponent(thumbName(name) + '.png'));
  if (!body) return null;
  if (body.length < 512 && body[0] !== 0x89) {
    const target = new TextDecoder().decode(body).trim();
    if (/\.png$/i.test(target) && !target.includes('/')) return get(base + encodeURIComponent(target));
    return null;
  }
  return body;
}
