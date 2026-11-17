#!/usr/bin/env python3
"""Art for the Pixel theme, inspired by Rookervik's Pixel theme for EmulationStation
(github.com/RetroPie/es-theme-pixel): a dark mosaic of little bevelled blocks with a
glow in the system's colour behind the selected cart, a blocky frame, and a pixel-art
console for each system. All of it is drawn here, at the GBA's own resolution; none
of that theme's art is used.

The glow is one picture for every shelf: each shelf only recolours its palette bank
(theme.json "tint"), so it costs no extra tiles. The carts are the Shelf theme's, so
its label packs work here unchanged.

    python3 make_art.py <out dir> [shelf art dir]
"""
import json, math, os, random, shutil, sys
from PIL import Image, ImageDraw

SW, SH = 240, 160
B = 4                        # block size
BAND = 108                   # top of the frame band (two rows of blocks)
LIP = 116                    # the cart disappears below here
MOUTH = (64, 176)            # the slot's opening in the band
GLOW = (120, 62)

GRID = (14, 14, 18)
GREYS = [(36, 36, 42), (46, 46, 54), (58, 58, 68)]
NEUTRAL = [GRID] + GREYS
PANEL = (22, 22, 27)
FRAME = (150, 150, 162)
FRAME_HI = (196, 196, 206)
FRAME_LO = (92, 92, 104)
SLOT = (8, 8, 10)

HUES = {                      # the glow's colour per shelf
    'gba': (143, 133, 255),   # Pixel's own GBA colour
    'gb': (150, 178, 64),
    'gbc': (206, 92, 210),
    'nes': (236, 72, 72),
    'sms': (244, 146, 44),
    'gg': (48, 192, 206),
}
LEVELS = 5


def ramp(hue):
    """LEVELS glow colours, from just above the mosaic's greys to the hue."""
    base = GREYS[2]
    out = []
    for i in range(LEVELS):
        t = 0.22 + 0.5 * i / (LEVELS - 1)
        out.append(tuple(round(b + (h * 0.78 - b) * t) for b, h in zip(base, hue)))
    return out


def tint_colours(ext):
    return NEUTRAL + ramp(HUES[ext])


def block(px, bx, by, face):
    """A 4x4 block: its face, with a dark grid line on its right and bottom."""
    for y in range(B):
        for x in range(B):
            px[bx + x, by + y] = GRID if x == B - 1 or y == B - 1 else face


# Each 8x8 tile holds 2x2 blocks; their shades come from a short list of patterns
# so the mosaic repeats in tiles and stays cheap.
PATTERNS = [(0, 0, 0, 1), (0, 1, 0, 0), (1, 0, 0, 0), (0, 0, 1, 0), (0, 0, 0, 0),
            (1, 0, 0, 1), (0, 1, 1, 0), (0, 0, 0, 0)]


def mosaic(ext):
    img = Image.new('RGB', (SW, SH), GRID)
    px = img.load()
    rnd = random.Random(7)
    glow = ramp(HUES[ext])
    for ty in range(SH // 8):
        for tx in range(SW // 8):
            cx, cy = tx * 8 + 4, ty * 8 + 4
            # how far from the glow's centre, as an ellipse
            d = math.hypot((cx - GLOW[0]) / 118, (cy - GLOW[1]) / 66)
            pat = rnd.choice(PATTERNS)
            if d < 1.0 and ty < BAND // 8:          # whole tiles above the frame
                lvl = min(LEVELS - 2, int((1.0 - d) * LEVELS * 0.95 + rnd.random() * 0.6))
                shades = [glow[lvl + p] for p in pat]
            else:
                g = 1 if d < 1.45 else 0
                if rnd.random() < 0.2:
                    g = max(0, g - 1)
                shades = [GREYS[g + p] for p in pat]
            for k, (bx, by) in enumerate([(0, 0), (4, 0), (0, 4), (4, 4)]):
                block(px, tx * 8 + bx, ty * 8 + by, shades[k])
    return img


# ---------------------------------------------------------------- consoles
def rrect(d, box, r, fill):
    d.rounded_rectangle(box, r, fill=fill)


def outline(img, col):
    """A 1 px outline around the opaque shape."""
    a = img.getchannel('A').load()
    px = img.load()
    w, h = img.size
    edge = []
    for y in range(h):
        for x in range(w):
            if a[x, y] == 0 and any(0 <= x + dx < w and 0 <= y + dy < h and a[x + dx, y + dy]
                                    for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1))):
                edge.append((x, y))
    for x, y in edge:
        px[x, y] = col + (255,)


def dpad(d, cx, cy, col):
    d.rectangle((cx - 1, cy - 3, cx + 1, cy + 3), fill=col)
    d.rectangle((cx - 3, cy - 1, cx + 3, cy + 1), fill=col)


def dot(d, cx, cy, col, r=1):
    d.rectangle((cx - r, cy - r, cx + r, cy + r), fill=col)
    if r > 1:
        d.point([(cx - r, cy - r), (cx + r, cy - r), (cx - r, cy + r), (cx + r, cy + r)], fill=(0, 0, 0, 0))


def screen(d, box, bezel, glass, shine):
    x0, y0, x1, y1 = box
    rrect(d, box, 2, bezel)
    d.rectangle((x0 + 3, y0 + 3, x1 - 3, y1 - 3), fill=glass)
    d.line((x0 + 4, y0 + 4, x0 + 7, y0 + 4), fill=shine)
    d.point((x0 + 4, y0 + 5), fill=shine)


def con_gba():
    img = Image.new('RGBA', (52, 26), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    body, hi, lo = (96, 86, 206), (138, 128, 246), (62, 54, 150)
    rrect(d, (1, 2, 50, 24), 8, lo)
    rrect(d, (1, 1, 50, 22), 8, body)
    d.line((8, 2, 43, 2), fill=hi)
    screen(d, (14, 3, 37, 19), (44, 42, 60), (126, 150, 112), (176, 196, 160))
    dpad(d, 7, 11, (34, 30, 60))
    dot(d, 44, 9, (60, 52, 140), 1)
    dot(d, 41, 13, (60, 52, 140), 1)
    d.point([(5, 18), (6, 18), (45, 18), (46, 18)], fill=lo)
    outline(img, (16, 14, 34))
    return img


def con_gb(colour=False):
    img = Image.new('RGBA', (24, 26), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    if colour:
        body, hi, lo = (124, 62, 176), (164, 102, 214), (86, 40, 128)
        bezel, glass, shine = (30, 28, 36), (96, 170, 210), (190, 230, 250)
    else:
        body, hi, lo = (192, 192, 182), (222, 222, 214), (146, 146, 138)
        bezel, glass, shine = (82, 84, 100), (132, 160, 48), (176, 198, 96)
    d.polygon([(1, 1), (22, 1), (22, 19), (17, 24), (1, 24)], fill=lo)
    d.polygon([(1, 1), (22, 1), (22, 18), (17, 23), (1, 23)], fill=body)
    d.line((2, 2, 21, 2), fill=hi)
    screen(d, (3, 3, 20, 14), bezel, glass, shine)
    if colour:
        d.rectangle((7, 7, 9, 9), fill=(240, 200, 60))      # a little colourful picture
        d.rectangle((13, 9, 16, 11), fill=(230, 80, 90))
    dpad(d, 6, 18, (34, 34, 40))
    btn = (60, 20, 80) if colour else (156, 30, 88)
    dot(d, 18, 17, btn, 1)
    dot(d, 15, 20, btn, 1)
    outline(img, (24, 22, 28))
    return img


def con_nes():
    img = Image.new('RGBA', (56, 24), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    light, lhi, mid, dark = (200, 200, 194), (226, 226, 220), (150, 150, 146), (54, 54, 58)
    d.rectangle((1, 1, 54, 22), fill=light)
    d.line((1, 1, 54, 1), fill=lhi)
    d.rectangle((6, 3, 36, 9), fill=mid)                   # the lid
    for x in range(8, 36, 3):
        d.line((x, 4, x, 8), fill=light)
    d.rectangle((40, 3, 52, 9), fill=mid)                  # ridges on the right
    for y in range(4, 9, 2):
        d.line((41, y, 51, y), fill=light)
    d.rectangle((1, 12, 54, 22), fill=dark)                # the front
    d.line((1, 12, 54, 12), fill=(30, 30, 32))
    d.rectangle((3, 15, 14, 20), fill=light)               # power and reset
    d.rectangle((4, 16, 7, 19), fill=(120, 120, 118))
    d.rectangle((10, 16, 13, 19), fill=(120, 120, 118))
    d.point((16, 16), fill=(230, 40, 40))
    d.rectangle((40, 15, 44, 19), fill=(24, 24, 26))       # controller ports
    d.rectangle((47, 15, 51, 19), fill=(24, 24, 26))
    d.line((20, 17, 36, 17), fill=(80, 80, 84))
    outline(img, (20, 20, 22))
    return img


def con_sms():
    img = Image.new('RGBA', (56, 24), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    black, hi, red, grey = (34, 34, 38), (64, 64, 72), (206, 40, 44), (120, 120, 126)
    d.polygon([(6, 3), (49, 3), (54, 9), (54, 22), (1, 22), (1, 9)], fill=black)
    d.line((6, 3, 49, 3), fill=hi)
    d.line((1, 9, 54, 9), fill=hi)
    d.rectangle((1, 13, 54, 14), fill=red)                 # the red stripe
    d.rectangle((14, 5, 41, 7), fill=(20, 20, 22))         # card slot / grille
    for x in range(16, 41, 3):
        d.point((x, 6), fill=hi)
    d.rectangle((4, 17, 8, 19), fill=grey)
    d.rectangle((11, 17, 15, 19), fill=grey)
    d.point((46, 18), fill=red)
    d.line((48, 18, 51, 18), fill=hi)
    outline(img, (10, 10, 12))
    return img


def con_gg():
    img = Image.new('RGBA', (56, 26), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    body, hi, lo = (40, 40, 46), (74, 74, 84), (24, 24, 28)
    rrect(d, (1, 2, 54, 24), 8, lo)
    rrect(d, (1, 1, 54, 22), 8, body)
    d.line((9, 2, 46, 2), fill=hi)
    screen(d, (15, 3, 40, 20), (22, 22, 26), (90, 160, 210), (180, 220, 245))
    d.rectangle((19, 12, 25, 16), fill=(80, 190, 90))      # a little picture
    d.rectangle((30, 8, 34, 10), fill=(240, 210, 70))
    dpad(d, 8, 11, (16, 16, 18))
    dot(d, 47, 9, (70, 70, 80), 1)
    dot(d, 44, 13, (70, 70, 80), 1)
    d.point((48, 17), fill=(40, 90, 200))                  # start
    d.point((49, 17), fill=(40, 90, 200))
    outline(img, (8, 8, 10))
    return img


CONSOLES = {'gba': con_gba, 'gb': con_gb, 'gbc': lambda: con_gb(True), 'nes': con_nes,
            'sms': con_sms, 'gg': con_gg}


def background(ext):
    img = mosaic(ext).convert('RGBA')
    d = ImageDraw.Draw(img)
    d.rectangle((0, LIP, SW, SH), fill=PANEL)               # the lower panel (frame drawn in front)
    d.rectangle((MOUTH[0], BAND, MOUTH[1] - 1, LIP - 1), fill=SLOT)   # inside the slot
    c = CONSOLES[ext]()
    cx = 8 + (56 - c.width) // 2
    img.alpha_composite(c, (cx, SH - B - c.height))
    return img.convert('RGB')


def frame_block(px, x, y):
    for yy in range(B):
        for xx in range(B):
            if xx == B - 1 or yy == B - 1:
                c = FRAME_LO
            elif xx == 0 or yy == 0:
                c = FRAME_HI
            else:
                c = FRAME
            px[x + xx, y + yy] = c + (255,)


def front():
    """The frame, in front of the carts: the band with the slot's opening, the
    sides and bottom of the lower panel, and the panel's middle (where a cart
    going in disappears)."""
    img = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    px = img.load()
    d = ImageDraw.Draw(img)
    d.rectangle((MOUTH[0], LIP, MOUTH[1] - 1, SH - 1), fill=PANEL + (255,))
    for y in (BAND, BAND + B):
        for x in range(0, SW, B):
            if not (MOUTH[0] <= x < MOUTH[1]):
                frame_block(px, x, y)
    for y in range(LIP, SH, B):
        frame_block(px, 0, y)
        frame_block(px, SW - B, y)
    for x in range(0, SW, B):
        frame_block(px, x, SH - B)
    # the slot's lips: one block row at either side of the opening, a step lower
    for x in (MOUTH[0] - B, MOUTH[1]):
        frame_block(px, x, LIP)
    return img


def main():
    out = sys.argv[1]
    shelf = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(__file__), '..', 'shelf', 'art')
    os.makedirs(out, exist_ok=True)
    for ext in HUES:
        background(ext).save(os.path.join(out, f'background_{ext}.png'))
    f = front()
    f.crop((0, BAND, SW, SH)).save(os.path.join(out, 'frame_front.png'))
    Image.new('RGBA', (SW, SH - BAND), (0, 0, 0, 0)).save(os.path.join(out, 'frame_back.png'))
    for name in ('cart.png', 'gb_shelf.png', 'gbc_shelf.png', 'nes_shelf.png', 'sms_shelf.png', 'gg_shelf.png'):
        shutil.copy(os.path.join(shelf, name), os.path.join(out, name))
    json.dump({ext: [list(c) for c in tint_colours(ext)] for ext in HUES},
              open(os.path.join(out, 'tints.json'), 'w'))


if __name__ == '__main__':
    main()
