# Building from source

EverFront is plain C for the GBA (a multiboot-style image that runs from EWRAM), with Python tools that build the themes and label packs.

## What you need

- `gcc-arm-none-eabi` (any recent version; the builds are made with the Debian/Ubuntu package)
- Python 3 with Pillow (`pip install pillow`)
- krikzz's [gba-pro-pub](https://github.com/krikzz/gba-pro-pub): the build copies the boot logo from its `edio/edio_mb.gba`, so this repo doesn't carry it
- [slotPlus](https://github.com/nelynes/slotPlus): the Slot theme is built from its `assets` folder

## Build

```
git clone https://github.com/krikzz/gba-pro-pub ../gba-pro-pub
git clone https://github.com/nelynes/slotPlus ../nelynes/slotPlus
sh build.sh         # everfront.gba, build/slot.thm, shelf.thm, artbook.thm
sh build_mini.sh    # everfront-mini.gba (EverDrive GBA X5 Mini)
```

`LOGO_FROM` and `SLOT_ASSETS` override where those two are found. `LABELS=<folder of GBA scans>` also builds `build/labels.bin` and `build/labels-80x41.bin`.

## Layout

| Path | What it is |
| --- | --- |
| `src/main.c` | The shelf: themes, carts, labels, input, settings, systems |
| `src/launch.c` | Starting games on the Pro: rom loading, saves, clock, sensors, in-game menu, emulators |
| `src/plat.h`, `src/plat_pro.c` | The platform layer over the Pro's file API |
| `src/mini/` | The X5 Mini port (FatFs over krikzz's Mini SD driver) |
| `src/ed/`, `src/edmini/` | krikzz's EverDrive drivers (MIT) |
| `src/fatfs/` | FatFs by ChaN (Mini build) |
| `themes/slot/theme.json` | The Slot theme: layout, plus which Slot art goes where |
| `themes/shelf/` | The Shelf theme: `make_art.py` draws all of its art |
| `themes/artbook/` | The Art Book theme: `make_art.py` draws all of its art |
| `themes/carts/` | Cart art for the other systems |
| `tools/make_theme.py` | Builds a `.thm` theme file from a `theme.json` |
| `tools/make_pack.py` | Builds label packs |
| `test/` | Emulator test builds with a fake SD card (`build_mock.sh`, `build_mock_mini.sh`) |
| `docs/label-maker/` | The Label Maker, published with GitHub Pages: plain JavaScript modules, no build step. `convert.js` is `make_pack.py`'s conversion and `pack.js` its file format. Art bases can be overridden with `?plusart=`, `?art=`, `?dat=` and `?thumbs=`. |

## File formats

- **Theme (`.thm`):** magic `EFTH`, version 1. s16 layout parameters, then palette, tiles, maps and cart sections, and a table of per-system carts (size, label area, pitch, foot, name row, optional background). A layout with `"name_on_bar": true` draws the game's name onto the bar's own pixels, in its palette, instead of on a clear row. `tools/make_theme.py` is the reference.
- **Label pack (`.bin`):** magic `EFLB`, version 3. Label width, height and colour count, then sorted `{key, offset}` pairs (keys are crc32s of the rom's full name, its title, or its game code), then per label 36 BGR555 colours and one byte per pixel. `tools/make_pack.py` documents it in full.
