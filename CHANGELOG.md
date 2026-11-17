# Changelog

## Unreleased

### Added
- **[Label Maker](https://nelynes.github.io/everfront/label-maker/):** a web page that makes every label pack for your card.
  - Open the SD card in Chrome or Edge, and it finds every game on every shelf and identifies it by file name or checksum.
  - Each game gets the best label available: your own scans, then labels already on the card, then the slotPlus and Slot art sets, then the game's logo on a colour from its box art (as Cart Studio+ makes them).
  - You can swap or remove any label, then write all the packs straight to the card. In other browsers you get a zip.

### Fixed
- `make_pack.py`: games whose names have accented capitals (É, Ö…) now get their labels. Keys lowercase only A–Z, as EverFront does on the GBA.

## 0.7 — 2026-11-10

### Added
- **Shelves for Game Boy, Game Boy Color, NES, Master System and Game Gear.** Up/Down switches shelves. Games run through the emulators in `edgba/edapp/<system>/`, the same ones the EverDrive menu uses (Goomba, PocketNES, SMSAdvance). A shelf appears only when its system has an emulator and games.
- The GBC shelf also lists `.gb` games.
- Each shelf keeps its own favourites and recently played (`favourites-<system>.txt`, `recent-<system>.txt`), and EverFront starts on the shelf you used last.
- **Carts for every system on both themes.**
  - Slot: Slot's own GB and GBC carts, plus new NES, Master System and Game Gear carts by Nelynes (the Master System and Game Gear ones in black).
  - Shelf: flat, stylized carts for every system, drawn to match its GBA cart.
- On the handheld shelves, carts are much taller (up to 96 px on Slot), and the Shelf theme lowers its plank to make room.
- On the Shelf theme, the header shows the current system's name.
- Label pack tool: `--also` (a second folder of scans) and `--alias` (match renamed roms or hacks to a scan). It now builds packs for every system.

### Fixed
- The display is fully reset at start-up: DMA, scroll, window and blend registers, and sprites.
- Plain labels fit labels of any size.
- Theme builder: system carts no longer shift palette colours, and very dark carts no longer show stray label-coloured pixels.
- Label pack tool no longer fails on scans with very few colours.

### Known
- Emulated games have no EverDrive in-game menu, and saves are written only when you open the emulator's menu (see the README).

## 0.6 — 2026-11-01

### Added
- **Favourites** (SELECT toggles a favourite, marked `*`) and **recently played** (the last 20), with Settings › Show to filter the shelf to either.
- **Sensors:** Boktai's solar sensor and tilt or gyro games are set up as from the EverDrive menu.
- **Up to 3000 games** (was 600). Names are read on demand.
- Full label packs: up to 4000 labels per pack.

### Changed
- SELECT no longer switches theme; use Settings.

## 0.5 — 2026-10-31

### Added
- **Settings menu** (START): Theme, Clock on/off, Show log, Close. Choices are saved in `everfront/config.txt`.
- **Better logging:** each session starts with the date and time. Launches, errors and settings changes are logged, and the log is trimmed when it grows past 64 KB.

### Tested
- Start Mode-A: the cart powers up straight into EverFront.

## 0.4 — 2026-10-30

### Changed
- **Shelf theme v2:** bigger 96×58 carts with 80×41 labels, and the console sits lower. Its label pack is now `labels-80x41.bin` (it was `labels-60x31.bin`).
- The font is now Unscii 8 (public domain), replacing the EverDrive menu's font.

### Added
- START shows the log on screen.

## 0.3 — 2026-10-29

First shared build.
- The shelf, with Slot and Shelf themes in `everfront/themes`.
- GBA games boot with their saves, clock and the L+R in-game menu (firmware 26.0127).
- Label packs built from your own scans with `make_pack.py`.
- Early test build for the EverDrive GBA X5 Mini (GBA only, saves off).
