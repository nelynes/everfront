#!/usr/bin/env python3
"""Draws EverFront's own cart pictures for the systems a theme has no art
for: an NES-style cartridge, and plain Game Boy / Game Boy Color style
cartridges (no logos, no real labels). Drawn large; themes scale them.

usage: make_carts.py <out dir>   (nes.png, gb.png, gbc.png at 4x)
"""
import os, sys
from PIL import Image, ImageDraw

K = 4

def nes(w=64, h=72):
    W, H = w * K, h * K
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    body, dark, light = (150, 150, 152), (96, 96, 100), (190, 190, 192)
    d.rounded_rectangle((0, 0, W - 1, H - 1), 3 * K, fill=dark)
    d.rounded_rectangle((K, K, W - 1 - K, H - 1 - K), 2 * K, fill=body)
    d.rectangle((2 * K, K, W - 2 * K, 3 * K), fill=light)                 # top edge
    d.rectangle((5 * K, 3 * K, W - 5 * K, 46 * K), fill=(112, 112, 116))   # label recess
    for i in range(6):                                                      # grip ridges
        y = (50 + i * 3) * K
        d.rectangle((6 * K, y, W - 6 * K, y + K), fill=dark)
    d.rectangle((0, H - 6 * K, W - 1, H - 1), fill=(80, 80, 84))           # connector edge
    return im

def gb(w=68, h=72, colour=(176, 176, 180), dark=(120, 120, 126), well=(150, 150, 156)):
    W, H = w * K, h * K
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle((0, 0, W - 1, H - 1), 3 * K, fill=dark)
    d.rounded_rectangle((K, K, W - 1 - K, H - 1 - K), 2 * K, fill=colour)
    d.polygon([(W - 9 * K, 0), (W, 0), (W, 9 * K)], fill=(0, 0, 0, 0))    # the cut corner
    d.line((W - 9 * K, 0, W - 1, 9 * K), fill=dark, width=K)
    for i in range(4):                                                      # top ridges
        y = (3 + i * 2) * K
        d.line((8 * K, y, W - 14 * K, y), fill=dark, width=K // 2)
    lx, ly = int(w * .147) * K, int(h * .29) * K
    d.rounded_rectangle((lx - K, ly - K, int(w * .854) * K + K, int(h * .87) * K + K), 2 * K, fill=well)
    d.polygon([(W // 2 - 4 * K, H - 5 * K), (W // 2 + 4 * K, H - 5 * K), (W // 2, H - 2 * K)], fill=dark)
    return im

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    nes().save(os.path.join(out, 'nes.png'))
    gb().save(os.path.join(out, 'gb.png'))
    gb(colour=(104, 96, 136), dark=(58, 52, 80), well=(132, 124, 160)).save(os.path.join(out, 'gbc.png'))

main()
