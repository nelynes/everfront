# Label art

Each theme reads one label pack per system from the `everfront` folder on the SD card. You build the packs on a computer from your own label scans, with `make_pack.py` from the release's `label tools` folder. Scans are the publishers' artwork, so no packs are included with EverFront.

## Build the packs

1. Install [Python 3](https://www.python.org/downloads/), then run `pip install pillow`.
2. Collect label scans as PNG files named like your roms, without the extension: `Metroid Fusion (USA).png` for `Metroid Fusion (USA).gba`. A scan named only by its title (`Metroid Fusion.png`) also matches. Keep each system's scans in its own folder.
3. From the `label tools` folder, build each pack you want, using its name and size from the table below:

   ```
   python make_pack.py "C:\path\to\GBA Labels" labels.bin
   python make_pack.py "C:\path\to\GBA Labels" labels-80x41.bin --size 80x41
   python make_pack.py "C:\path\to\NES Labels" labels-nes.bin --size 43x65
   python make_pack.py "C:\path\to\NES Labels" labels-nes-s.bin --size 35x55
   ```

4. Copy the `.bin` files into `everfront` on the card.

## Pack names and sizes

| System | Slot pack | Slot size | Shelf pack | Shelf size |
| --- | --- | --- | --- | --- |
| GBA | `labels.bin` | 88x45 (the default) | `labels-80x41.bin` | 80x41 |
| Game Boy | `labels-gb.bin` | 62x55 | `labels-gb-s.bin` | 53x46 |
| Game Boy Color | `labels-gbc.bin` | 62x56 | `labels-gbc-s.bin` | 53x47 |
| NES | `labels-nes.bin` | 43x65 | `labels-nes-s.bin` | 35x55 |
| Master System | `labels-sms.bin` | 106x15 | `labels-sms-s.bin` | 88x13 |
| Game Gear | `labels-gg.bin` | 79x53 | `labels-gg-s.bin` | 66x46 |

A pack built at the wrong size is ignored, and those carts show plain labels.

## Matching scans to roms

| Option | What it does |
| --- | --- |
| `--also <folder>` | Adds scans from another folder where the main one has none. Use your Game Boy scans for the GBC packs, since the GBC shelf also runs `.gb` games. Can be given more than once. |
| `--alias <file>` | Lines like `Duck Tales = DuckTales (USA)` (rom name = scan name, no extensions) for roms whose names don't match a scan: hacks, translations, renames. See `aliases-example.txt`. |
| `--dat <file>` | A GBA No-Intro or libretro `.dat` that lists serials. GBA labels then also match by the game code in each rom's header. |
| `--review <sheet.png>` | Saves a contact sheet of the converted labels to check them. |
| `--cache <folder>` | Keeps converted labels, so later runs only convert new or changed scans. |

Run `python make_pack.py` with no arguments to see every option.

Example: a GBC pack with GB scans as a fallback, and aliases for two hacks:

```
python make_pack.py "C:\Scans\GBC" labels-gbc.bin --size 62x56 --also "C:\Scans\GB" --alias my-aliases.txt
```

with `my-aliases.txt`:

```
Metroid II - Color (ejrtq) v1.3 = Metroid II - Return of Samus (World)
Super Mario Land 2 DX = Super Mario Land 2 - 6 Golden Coins (USA, Europe) (Rev 2)
```
