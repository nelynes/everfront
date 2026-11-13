# EverFront user guide

Everything you need to install EverFront, use it, add other systems and make label art. For a quick look at what EverFront is, see the [README](README.md).

**Contents**
- [What you need](#what-you-need)
- [Install](#install)
- [Using it](#using-it)
- [Other systems](#other-systems)
- [Label art](#label-art)
- [Known limits](#known-limits)
- [Troubleshooting](#troubleshooting)
- [EverDrive GBA X5 Mini](#everdrive-gba-x5-mini)
- [How it works](#how-it-works)

## What you need

| Item | Requirement |
| --- | --- |
| Cart | An EverDrive GBA Pro. The X5 Mini has its own test build (see [EverDrive GBA X5 Mini](#everdrive-gba-x5-mini)). |
| Firmware | 26.0127 for the L+R in-game menu. Other versions still launch games, but the in-game menu stays off. Tested on 26.0127 only. |
| SD card | Already set up for the EverDrive (the `edgba` folder from krikzz's firmware is on it). |
| Games | Unzipped `.gba` files in a folder named `Game Boy Advance` or `GBA` at the card's root (or at the root itself). Up to 3000 games; subfolders are not searched. |
| Other systems (optional) | An emulator per system in `edgba/edapp/<system>/`, plus a games folder for it (see [Other systems](#other-systems)). |

To check the firmware, open the EverDrive menu and look at Device Info.

## Install

1. Download `everfront-<version>.zip` from the [latest release](../../releases/latest).
2. Put the EverDrive's SD card in your computer.
3. Unzip the release and copy the `everfront` folder from its `SD card` folder to the root of the card, next to `edgba`.
4. Make sure your games are in `Game Boy Advance` or `GBA` at the root of the card.
5. Optional: add emulators and games for other systems ([Other systems](#other-systems)) and your label packs ([Label art](#label-art)).
6. Put the card back in the cart and switch on.
7. In the EverDrive menu, open `everfront`, select `everfront.gba` and press A:
   - **Start Game** starts EverFront this once.
   - **Start Mode-A** starts it now and straight away at every power-up.

To update, copy the new `everfront` folder over the old one. Saves, favourites and settings are kept.

## Using it

| Button | What it does |
| --- | --- |
| Left / Right | One cart along; hold to scroll faster. The shelf wraps from the last game to the first. |
| L / R | Five carts along |
| Up / Down | Next or previous shelf: GBA, Game Boy, Game Boy Color, NES, Master System, Game Gear (only those with games and an emulator) |
| A | Put the cart in the slot and start the game |
| SELECT | Add the game to your favourites or take it off (favourites are marked with a `*`) |
| START | Settings: Theme, Show (all games, favourites or recently played), Clock on or off, Show log, Close |
| B | Back to the shelf after an error message, or close the log or Settings |
| L + R in a GBA game | The EverDrive's in-game menu: save and load states, reset, exit |

Saves work exactly as with the EverDrive menu: they live in `edgba/gamedata`, so a game started from either menu sees the same save. Exit Game in the in-game menu returns to the EverDrive menu, not to EverFront. Boktai's solar sensor and tilt or gyro games (Yoshi Topsy-Turvy, WarioWare Twisted) use the cart's own sensors, as they do from the EverDrive menu.

## Other systems

EverFront includes no emulators. Each system uses the emulator the EverDrive menu would use, one `.gba` per folder (or one named `app.gba`). An optional `config.txt` beside it is read as the EverDrive menu reads it.

| System | Emulator folder | Example emulator | Games folder | Files |
| --- | --- | --- | --- | --- |
| Game Boy | `edgba/edapp/gb/` | Goomba | `Game Boy` or `GB` | `.gb` |
| Game Boy Color | `edgba/edapp/gbc/` | Goomba Color | `Game Boy Color` or `GBC` | `.gbc` and `.gb` |
| NES | `edgba/edapp/nes/` | PocketNES | `NES` or `Nintendo` | `.nes` |
| Master System | `edgba/edapp/sms/` | SMSAdvance | `Master System` or `SMS` | `.sms` |
| Game Gear | `edgba/edapp/gg/` | SMSAdvance | `Game Gear` or `GG` | `.gg` |

A shelf appears when its system has both an emulator and games. In these games L+R opens the emulator's own menu, not the EverDrive's. Its Exit restarts the game, so you leave an emulated game by switching off.

> [!IMPORTANT]
> **Saving in emulated games:** the emulators only write a game's save to the cart when you open their menu (L+R) or use Exit there. Before you switch off, press L+R so the save is written, then power off. Switching straight off loses what you saved since.

## Label art

Each theme reads one label pack per system from the `everfront` folder on the SD card. You build the packs on a computer from your own label scans, with `make_pack.py` from the release's `label tools` folder. Scans are the publishers' artwork, so no packs are included with EverFront.

### Build the packs

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

### Pack names and sizes

| System | Slot pack | Slot size | Shelf pack | Shelf size |
| --- | --- | --- | --- | --- |
| GBA | `labels.bin` | 88x45 (the default) | `labels-80x41.bin` | 80x41 |
| Game Boy | `labels-gb.bin` | 62x55 | `labels-gb-s.bin` | 53x46 |
| Game Boy Color | `labels-gbc.bin` | 62x56 | `labels-gbc-s.bin` | 53x47 |
| NES | `labels-nes.bin` | 43x65 | `labels-nes-s.bin` | 35x55 |
| Master System | `labels-sms.bin` | 106x15 | `labels-sms-s.bin` | 88x13 |
| Game Gear | `labels-gg.bin` | 79x53 | `labels-gg-s.bin` | 66x46 |

A pack built at the wrong size is ignored, and those carts show plain labels.

### Matching scans to roms

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

## Known limits

- **Exit Game** goes back to the EverDrive menu, not to the shelf.
- **Emulated games** have no EverDrive in-game menu (the emulator's menu replaces it), and you leave them by switching off. Open their menu first so the save is written.
- **Cheats** are not supported.
- **Games:** one folder per system, no zips.
- **Firmware:** the in-game menu needs firmware 26.0127. A firmware update may turn it off until EverFront is updated too.

## Troubleshooting

| What you see | What to do |
| --- | --- |
| "Theme file missing" | `themes/slot.thm` is not in `everfront/themes`. Copy the `themes` folder from the zip again. |
| "Theme file: wrong format" or another "Theme…" message | The theme file is damaged or from a different version. Copy it again from the same zip as the `.gba`. |
| "NO GAMES FOUND" | No games in `Game Boy Advance`, `GBA` or the root. Check the folder name and that the games are unzipped. |
| A system's shelf is missing | Its emulator isn't in `edgba/edapp/<system>/`, or its games folder is empty or named differently. |
| Plain coloured labels | No label pack for this theme and system, or the scans' names don't match the roms. Rebuild the pack, with `--alias` for renamed roms. |
| An emulated game lost its progress | It was switched off without opening the emulator's menu first (see [Other systems](#other-systems)). |
| A game won't start, or an error after A | Press B to get back. Try the game from the EverDrive menu to rule out the rom itself. |
| L+R does nothing in a GBA game | The firmware isn't 26.0127, so the in-game menu is off. |

EverFront writes a log to `everfront/log.txt` on the card. Please attach it to any [issue](../../issues) you open.

## EverDrive GBA X5 Mini

Each release also has `everfront-mini-<version>.zip`, an early test build for the X5 Mini. It is behind the Pro version:

| | Pro | X5 Mini test build |
| --- | --- | --- |
| Shelf, both themes, GBA labels | Yes | Yes |
| GBA games start | Yes | Yes |
| Saves | Yes | **Not yet: games run without saving** |
| Other systems' shelves | Yes | Not yet |
| Remembers favourites, recent games and settings | Yes | Not yet (it doesn't write to the SD card) |
| Game limit | 3000 | 600 |

It never writes to the SD card or the cart's save memory, so your existing saves are safe. Install it the same way, but start `everfront-mini.gba`. The zip's `README-MINI-TEST.txt` lists what's worth testing and reporting.

## How it works

EverFront is an ordinary GBA program that the EverDrive loads like a game. A few details explain its behaviour:

- **Starting games.** EverFront starts a game the same way the EverDrive's own menu does: it loads the rom, works out the save type, sets up the clock and sensors, and hands over. That's why saves, the clock and the in-game menu behave the same, and why saves are shared with the EverDrive menu (`edgba/gamedata`).
- **The in-game menu** belongs to the EverDrive's firmware. EverFront contains none of that code: it reads the menu from the cart's own firmware each time a game starts. This depends on the firmware's layout, so it is tied to firmware 26.0127. On any other version EverFront notices the mismatch and starts games without the in-game menu.
- **Emulated systems** use the emulators the EverDrive menu uses, from `edgba/edapp/<system>/`. The emulators take over L+R for their own menus, which is why the EverDrive's in-game menu isn't available there.
- **Themes** are files (`everfront/themes/*.thm`) holding all the art and layout, so the program itself stays tiny (about 44 KB).
- **Settings and lists** are plain text files in `everfront`: `config.txt`, `favourites.txt`, `recent.txt` (with `-<system>` versions for the other shelves), and the log, `log.txt`.

To build EverFront or make a theme, see [Building from source](docs/building.md).
