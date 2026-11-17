#!/bin/sh
# Builds everfront.gba and the three themes. Needs gcc-arm-none-eabi and
# Python 3 with Pillow. See docs/building.md.
# LOGO_FROM: krikzz's edio_mb.gba (gba-pro-pub/edio/edio_mb.gba).
# SLOT_ASSETS: slotPlus's assets folder (the Slot theme's art).
set -e
cd "$(dirname "$0")"
LOGO_FROM=${LOGO_FROM:-../gba-pro-pub/edio/edio_mb.gba}
mkdir -p build
export SLOT_ASSETS=${SLOT_ASSETS:-../nelynes/slotPlus/assets}
# themes: the art goes in theme files on the SD card, not in the program
python3 tools/make_theme.py themes/slot/theme.json "$SLOT_ASSETS" build/slot.thm build/slot_preview.png
python3 themes/carts/make_carts.py themes/carts/art
python3 themes/carts/make_black.py
python3 themes/shelf/make_art.py themes/shelf/art
python3 tools/make_theme.py themes/shelf/theme.json themes/shelf/art build/shelf.thm build/shelf_preview.png
python3 themes/artbook/make_art.py themes/artbook/art
python3 tools/make_theme.py themes/artbook/theme.json themes/artbook/art build/artbook.thm build/artbook_preview.png
CFLAGS="-mcpu=arm7tdmi -mtune=arm7tdmi -mthumb -mthumb-interwork -O2 -Wall -Wno-pointer-sign -ffreestanding -fno-strict-aliasing -fno-tree-loop-distribute-patterns -Isrc"
OBJS=""
for f in src/main.c src/plat_pro.c src/launch.c src/mem.c src/ed/everdrive.c src/ed/printf.c src/ed/sys.c src/font.c; do
    o=build/$(basename "$f" .c).o
    arm-none-eabi-gcc $CFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
arm-none-eabi-gcc -mcpu=arm7tdmi -c crt0.s -o build/crt0.o
arm-none-eabi-gcc -mcpu=arm7tdmi -mthumb-interwork -nostartfiles -nostdlib -T gba_mb.ld \
    build/crt0.o $OBJS -lgcc -Wl,-Map,build/everfront.map -o build/everfront.elf
arm-none-eabi-objcopy -O binary build/everfront.elf build/everfront.bin
python3 fix_header.py build/everfront.bin everfront.gba "$LOGO_FROM" 0
arm-none-eabi-size build/everfront.elf
# label packs, when LABELS names a folder of label scans
if [ -n "$LABELS" ]; then
    # DAT: optional No-Intro / libretro GBA .dat with serials (matches by game code)
    python3 tools/make_pack.py "$LABELS" build/labels.bin ${DAT:+--dat "$DAT"}
    python3 tools/make_pack.py "$LABELS" build/labels-80x41.bin --size 80x41 ${DAT:+--dat "$DAT"}
fi
