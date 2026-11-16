// EverFront's shelves: where their games live on the card, how libretro and the art sets know
// them, and the label packs each theme reads. Folder names and pack sizes are EverFront 0.7's
// (src/main.c `systems`, themes/*/theme.json).

export const SYSTEMS = [
  {
    id: 'gba',
    name: 'Game Boy Advance',
    folders: ['Game Boy Advance', 'GBA', ''], // '' = the card's root
    exts: { gba: 'Nintendo - Game Boy Advance' },
    packs: [
      { theme: 'Slot', file: 'labels.bin', w: 88, h: 45 },
      { theme: 'Shelf', file: 'labels-80x41.bin', w: 80, h: 41 },
    ],
  },
  {
    id: 'gb',
    name: 'Game Boy',
    folders: ['Game Boy', 'GB'],
    exts: { gb: 'Nintendo - Game Boy' },
    packs: [
      { theme: 'Slot', file: 'labels-gb.bin', w: 62, h: 55 },
      { theme: 'Shelf', file: 'labels-gb-s.bin', w: 53, h: 46 },
    ],
  },
  {
    id: 'gbc',
    name: 'Game Boy Color',
    folders: ['Game Boy Color', 'GBC'],
    // The GBC shelf runs .gb games too.
    exts: { gbc: 'Nintendo - Game Boy Color', gb: 'Nintendo - Game Boy' },
    packs: [
      { theme: 'Slot', file: 'labels-gbc.bin', w: 62, h: 56 },
      { theme: 'Shelf', file: 'labels-gbc-s.bin', w: 53, h: 47 },
    ],
  },
  {
    id: 'nes',
    name: 'NES',
    folders: ['NES', 'Nintendo'],
    exts: { nes: 'Nintendo - Nintendo Entertainment System' },
    headerless: 16, // iNES header: libretro also lists each game without it
    packs: [
      { theme: 'Slot', file: 'labels-nes.bin', w: 43, h: 65 },
      { theme: 'Shelf', file: 'labels-nes-s.bin', w: 35, h: 55 },
    ],
  },
  {
    id: 'sms',
    name: 'Master System',
    folders: ['Master System', 'SMS'],
    exts: { sms: 'Sega - Master System - Mark III' },
    packs: [
      { theme: 'Slot', file: 'labels-sms.bin', w: 106, h: 15 },
      { theme: 'Shelf', file: 'labels-sms-s.bin', w: 88, h: 13 },
    ],
  },
  {
    id: 'gg',
    name: 'Game Gear',
    folders: ['Game Gear', 'GG'],
    exts: { gg: 'Sega - Game Gear' },
    packs: [
      { theme: 'Slot', file: 'labels-gg.bin', w: 79, h: 53 },
      { theme: 'Shelf', file: 'labels-gg-s.bin', w: 66, h: 46 },
    ],
  },
];
