#!/bin/sh
# Builds everfront-mini.gba, the EverDrive GBA X5 Mini build (stage 1: games
# start without saves). Same shelf and themes as the Pro build; underneath,
# krikzz's Mini SD driver (src/edmini, MIT) and FatFs (src/fatfs, read-only).
# Run build.sh first (it makes the themes).
set -e
cd "$(dirname "$0")"
LOGO_FROM=${LOGO_FROM:-../gba-pro-pub/edio/edio_mb.gba}
OUT=${OUT:-build/mini}
mkdir -p "$OUT"
CFLAGS="$MINIFLAGS -DPLAT_MINI -mcpu=arm7tdmi -mtune=arm7tdmi -mthumb -mthumb-interwork -O2 -Wall -Wno-pointer-sign -ffreestanding -fno-strict-aliasing -fno-tree-loop-distribute-patterns -ffunction-sections -fdata-sections -Isrc"
OBJS=""
SRCS="src/main.c src/mini/plat_mini.c src/mini/launch_mini.c src/mini/diskio_mini.c src/mem.c src/ed/printf.c src/font.c src/fatfs/ff.c src/fatfs/ffunicode.c ${MINI_DISK:-src/edmini/everdrive.c src/edmini/disk.c}"
for f in $SRCS; do
    o=$OUT/$(basename "$f" .c).o
    arm-none-eabi-gcc $CFLAGS -c "$f" -o "$o"
    OBJS="$OBJS $o"
done
arm-none-eabi-gcc -mcpu=arm7tdmi -c crt0.s -o $OUT/crt0.o
arm-none-eabi-gcc -mcpu=arm7tdmi -mthumb-interwork -nostartfiles -nostdlib -Wl,--gc-sections -T gba_mb.ld \
    $OUT/crt0.o $OBJS -lgcc -Wl,-Map,$OUT/everfront-mini.map -o $OUT/everfront-mini.elf
arm-none-eabi-objcopy -O binary $OUT/everfront-mini.elf $OUT/everfront-mini.bin
python3 fix_header.py $OUT/everfront-mini.bin ${TARGET:-everfront-mini.gba} "$LOGO_FROM" ${PAD:-0} EVFM
arm-none-eabi-size $OUT/everfront-mini.elf
