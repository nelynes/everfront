#!/bin/sh
# Emulator-only Mini build: everfront-mini with a fake SD driver and a FAT16
# image of a test card (games from mock_names.h, the themes, label packs)
# appended at 0x08100000. Needs mtools + dosfstools. Never on a cart.
set -e
cd "$(dirname "$0")/.."
OUT=build/mock-mini MINI_DISK=test/mock_mini.c TARGET=build/mock-mini/mock-mini.gba PAD=0x100000 sh build_mini.sh >/dev/null
IMG=build/mock-mini/card.img
rm -f $IMG
mkfs.vfat -C -F 16 -s 4 -n EVERFRONT $IMG 16384 >/dev/null
export MTOOLS_SKIP_CHECK=1
mmd -i $IMG ::everfront ::everfront/themes "::Game Boy Advance"
mcopy -i $IMG build/slot.thm build/shelf.thm build/artbook.thm ::everfront/themes/
mcopy -i $IMG build/labels.bin build/labels-60x31.bin ::everfront/
python3 - <<'PY'
import re, os, subprocess
names = re.findall(r'"([^"]+\.gba)"', open('test/mock_names.h').read())
os.makedirs('build/mock-mini/games', exist_ok=True)
for n in names:
    rom = bytearray(4096 + 37)              # odd size: exercises the last part sector
    rom[0xA0:0xAC] = b'MOCKGAME'.ljust(12, b'\0')
    rom[0xAC:0xB0] = b'XXXX'
    p = os.path.join('build/mock-mini/games', n)
    open(p, 'wb').write(rom)
    subprocess.check_call(['mcopy', '-i', 'build/mock-mini/card.img', p, '::Game Boy Advance/'])
print(len(names), 'games on the test card')
PY
cat $IMG >> build/mock-mini/mock-mini.gba
ls -l build/mock-mini/mock-mini.gba
