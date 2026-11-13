#!/bin/sh
# Emulator-only build with a fake cart and its SD files appended. Never on a cart.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/mock
CFLAGS="$MOCKFLAGS -mcpu=arm7tdmi -mthumb -mthumb-interwork -O2 -Wall -Wno-pointer-sign -ffreestanding -fno-strict-aliasing -fno-tree-loop-distribute-patterns -Isrc"
OBJS=""
for f in src/main.c src/plat_pro.c src/launch.c src/mem.c test/mock_ed.c src/ed/printf.c src/ed/sys.c src/font.c; do
    o=build/mock/$(basename "$f" .c).o
    arm-none-eabi-gcc $CFLAGS -c "$f" -o "$o"; OBJS="$OBJS $o"
done
arm-none-eabi-gcc -mcpu=arm7tdmi -c crt0.s -o build/mock/crt0.o
arm-none-eabi-gcc -mcpu=arm7tdmi -nostartfiles -nostdlib -T gba_mb.ld build/mock/crt0.o $OBJS -lgcc -o build/mock/mock.elf
arm-none-eabi-objcopy -O binary build/mock/mock.elf build/mock/mock.bin
python3 fix_header.py build/mock/mock.bin build/mock/mock.gba "$LOGO_FROM" 0x100000
# the SD files the shelf reads, as a little table at 0x08100000 (mock_ed.c)
python3 - <<'PY'
import struct, os
d = open('build/mock/mock.gba', 'rb').read()
assert len(d) == 0x100000
files = [] if os.environ.get('NOLABELS') else [('everfront/labels.bin', 'build/labels.bin'),
         ('everfront/labels-80x41.bin', 'build/labels-80x41.bin')]
files += [
         ('everfront/themes/shelf.thm', 'build/shelf.thm'),
         ('everfront/themes/slot.thm', 'build/slot.thm')]
# other systems: a fake emulator for gb and gbc (none for nes), a few games
import os
os.makedirs('build/mock/sd', exist_ok=True)
def fake(name, size):
    p = os.path.join('build/mock/sd', name.replace('/', '_'))
    open(p, 'wb').write(bytes(size))
    files.append((name, p))
fake('edgba/edapp/gb/goomba.gba', 4096)
fake('edgba/edapp/gbc/goomba.gba', 4096)
if os.environ.get('MOCKNES'): fake('edgba/edapp/nes/pocketnes.gba', 4096)
fake('edgba/edapp/gg/smsadvance.gba', 4096)
fake('edgba/edapp/sms/smsadvance.gba', 4096)
for n in ['Golden Axe (USA, Europe, Brazil).sms', 'Shinobi (USA, Europe, Brazil) (Rev 1).sms', 'Sonic The Hedgehog (USA, Europe, Brazil).sms']:
    fake('Master System/' + n, 262144)
for n in ['Shinobi II - The Silent Fury (World).gg', 'Sonic The Hedgehog - Triple Trouble (USA, Europe, Brazil) (En).gg', 'TaleSpin (USA, Europe, Brazil) (En).gg']:
    fake('Game Gear/' + n, 131072)
for n in ['Kirby\'s Dream Land (USA, Europe).gb', 'Tetris (World) (Rev 1).gb', 'Super Mario Land (World) (Rev 1).gb']:
    fake('Game Boy/' + n, 32768)
for n in ['Pokemon - Crystal Version (USA, Europe) (Rev 1).gbc', 'Legend of Zelda, The - Link\'s Awakening DX (USA, Europe) (Rev 2).gbc']:
    fake('Game Boy Color/' + n, 65536)
fake('NES/Super Mario Bros. (World).nes', 40976)
if os.environ.get('MOCKPACKS'):
    for f in sorted(os.listdir(os.environ['MOCKPACKS'])):
        if f.startswith('labels-') and f.endswith('.bin') and not f.startswith('labels-80'):
            files.append(('everfront/' + f, os.path.join(os.environ['MOCKPACKS'], f)))
if os.environ.get('MOCKCFG'):
    open('build/mock/sd/config.txt', 'w').write(os.environ['MOCKCFG'].replace('|', '\n') + '\n')
    files.append(('everfront/config.txt', 'build/mock/sd/config.txt'))
head = 4 + 104 * len(files)
table, blob = struct.pack('<I', len(files)), b''
for name, path in files:
    data = open(path, 'rb').read()
    table += name.encode().ljust(96, b'\0') + struct.pack('<II', head + len(blob), len(data))
    blob += data + b'\0' * ((4 - len(data) % 4) % 4)
open('build/mock/mock.gba', 'wb').write(d + table + blob)
PY
