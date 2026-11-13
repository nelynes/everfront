#!/usr/bin/env python3
"""Fill the GBA header of a raw binary and pad the file.

usage: fix_header.py <in.bin> <out.gba> <logo-source.gba> <pad-bytes> [game code]

The boot logo is copied from krikzz's prebuilt edio_mb.gba (gba-pro-pub),
so this repo doesn't carry it.
"""
import sys

src, dst, logo_src, pad = sys.argv[1], sys.argv[2], sys.argv[3], int(sys.argv[4], 0)
d = bytearray(open(src, 'rb').read())
logo = open(logo_src, 'rb').read()[4:0xA0]
d[4:0xA0] = logo
d[0xA0:0xAC] = b'EVERFRONT'.ljust(12, b'\0')
d[0xAC:0xB0] = (sys.argv[5] if len(sys.argv) > 5 else 'EVFR').encode()[:4].ljust(4, b'0')
d[0xB0:0xB2] = b'00'
d[0xB2] = 0x96
d[0xB3:0xBD] = bytes(10)
chk = 0
for b in d[0xA0:0xBD]:
    chk = (chk - b) & 0xFF
d[0xBD] = (chk - 0x19) & 0xFF
d[0xBE:0xC0] = bytes(2)
if len(d) < pad:
    d += bytes(pad - len(d))
open(dst, 'wb').write(d)
print(f'{dst}: {len(d)} bytes, header checksum {d[0xBD]:02X}')
