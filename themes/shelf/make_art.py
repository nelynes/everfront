#!/usr/bin/env python3
"""Draws the Shelf theme's art (the realistic-cart look from the second draft):
a dark wall with a wooden shelf, small realistic GBA carts, and the top of an
indigo GBA with its cart slot at the bottom of the screen.

usage: make_art.py <out dir>     (writes background.png, console_back.png,
                                  console_front.png, cart.png)
"""
import os, re, sys
from PIL import Image, ImageDraw

SW, SH = 240, 160
CW, CH = 96, 58          # cart on screen (GBA cart 57 x 35 mm)
LW, LH = 80, 41          # label well (scan aspect 800:413)
LX, LY = (CW - LW) // 2, 6
PLANK_Y = 80             # carts stand on the plank
PLANK_Y_LOW = 104        # the GB/GBC/NES shelves: a lower plank for taller carts
SLOT_Y = 140             # the console's slot lip
CON_Y = SLOT_Y - 14      # top of the console images

def font8():
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'src', 'font.c')).read()
    return [int(v, 16) for v in re.findall(r'0x([0-9A-Fa-f]{2})', src)][:96 * 8]

def text(img, s, x, y, col, font):
    for i, ch in enumerate(s):
        o = (ord(ch) - 32) * 8
        for r in range(8):
            bits = font[o + r]
            for b in range(8):
                if bits & (0x80 >> b):
                    img.putpixel((x + i * 8 + b, y + r), col)

def cart_body(k=4):
    W, H = CW * k, CH * k
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    base, dark, light = (128, 128, 136), (70, 70, 78), (172, 172, 180)
    d.rounded_rectangle((0, 0, W - 1, H - 1), 4 * k, fill=dark)
    d.rounded_rectangle((k, k, W - 1 - k, H - 1 - k), 3 * k, fill=base)
    for i in range(3 * k):
        a = 1 - i / (3 * k)
        d.line((4 * k, k + i, W - 4 * k, k + i), fill=tuple(int(base[c] + (light[c] - base[c]) * a * 0.8) for c in range(3)))
    for i in range(4 * k):
        a = i / (4 * k)
        y = H - 1 - k - 4 * k + i
        d.line((2 * k, y, W - 2 * k, y), fill=tuple(int(base[c] + (dark[c] - base[c]) * a * 0.7) for c in range(3)))
    for x0 in (0, W - 2 * k):
        d.rectangle((x0, H - 12 * k, x0 + 2 * k - 1, H - 8 * k), fill=(0, 0, 0, 0))
    for i in range(3):
        y = int(2.2 * k) + i * k
        d.line((14 * k, y, W - 14 * k, y), fill=(110, 110, 118))
    d.rounded_rectangle(((LX - 1) * k, (LY - 1) * k, (LX + LW + 1) * k - 1, (LY + LH + 1) * k - 1), 2 * k, fill=(100, 100, 108))
    for i in range(3):
        y = (LY + LH + 3 + i * 2) * k
        d.line((16 * k, y, W - 16 * k, y), fill=(104, 104, 112), width=max(1, k // 2))
    return im.resize((CW, CH), Image.LANCZOS)

def background(font, plank_y=PLANK_Y):
    im = Image.new('RGBA', (SW, SH))
    d = ImageDraw.Draw(im)
    for y in range(SH):
        t = y / SH
        t = int(t * 12) / 12                        # 12 bands: few colours for the 4 palette banks
        d.line((0, y, SW, y), fill=tuple(int(a + (b - a) * t) for a, b in zip((46, 44, 58), (26, 25, 34))))
    d.rectangle((0, 0, SW, 11), fill=(18, 17, 24))
    d.line((0, 11, SW, 11), fill=(60, 58, 76))
    d.rectangle((0, plank_y, SW, plank_y + 3), fill=(150, 104, 66))
    d.rectangle((0, plank_y + 4, SW, plank_y + 10), fill=(112, 74, 46))
    d.line((0, plank_y + 10, SW, plank_y + 10), fill=(70, 44, 28))
    for y in range(plank_y + 11, plank_y + 16):
        a = (y - plank_y - 11) / 5
        d.line((0, y, SW, y), fill=tuple(int(c * (0.55 + 0.45 * a)) for c in (30, 29, 40)))
    # the system's name (GAME BOY ADVANCE ...) is drawn here by the engine: theme.json system_name
    return im

def console():
    k = 4
    W, H = SW * k, (SH - CON_Y) * k
    back = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    front = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    indigo, indigo_d, indigo_l = (66, 60, 128), (44, 40, 92), (98, 92, 164)
    db, df = ImageDraw.Draw(back), ImageDraw.Draw(front)
    top = 14 * k
    df.rounded_rectangle((10 * k, top, W - 10 * k, H + 20 * k), 18 * k, fill=indigo)
    for i in range(3 * k):
        df.line((24 * k, top + i, W - 24 * k, top + i), fill=tuple(int(a + (b - a) * (1 - i / (3 * k))) for a, b in zip(indigo, indigo_l)))
    for x0 in (14 * k, W - 46 * k):
        df.rounded_rectangle((x0, top - 3 * k, x0 + 32 * k, top + 6 * k), 4 * k, fill=(150, 150, 168))
    # the slot: a dark opening behind the cart (back); the body in front
    # hides the cart once it is below the lip
    sx0, sx1 = (SW - CW) // 2 - 3, (SW + CW) // 2 + 3
    db.rounded_rectangle((sx0 * k, top - 6 * k, sx1 * k, top + 4 * k), 2 * k, fill=indigo_d)
    db.rectangle(((sx0 + 2) * k, top - 4 * k, (sx1 - 2) * k, top + 4 * k), fill=(14, 13, 22))
    df.rectangle((sx0 * k, top, sx1 * k, top + 2 * k), fill=indigo_d)
    front = hard(front.resize((SW, SH - CON_Y), Image.LANCZOS), 12)
    # power LED, drawn at screen size so it keeps its colour
    for x, y in ((SW - 29, 26), (SW - 28, 26), (SW - 29, 27), (SW - 28, 27)):
        front.putpixel((x, y), (120, 220, 120, 255))
    return hard(back.resize((SW, SH - CON_Y), Image.LANCZOS), 6), front

GREY = dict(base=(128, 128, 136), dark=(70, 70, 78), light=(172, 172, 180), edge=(58, 58, 66),
            well=(100, 100, 108), line=(106, 106, 114), hi=(150, 150, 158))
BLACK = dict(base=(64, 64, 72), dark=(22, 22, 26), light=(124, 124, 136), edge=(10, 10, 12),
             well=(30, 30, 34), line=(40, 40, 46), hi=(96, 96, 108))


def _shade(d, W, H, k, x0, x1, base, light, dark):
    """The Shelf carts' light: a soft highlight along the top, a shade at the foot."""
    for i in range(3 * k):
        a = 1 - i / (3 * k)
        d.line((x0 + 3 * k, k + i, x1 - 3 * k, k + i),
               fill=tuple(int(base[c] + (light[c] - base[c]) * a * 0.8) for c in range(3)))
    for i in range(4 * k):
        a = i / (4 * k)
        y = H - 1 - k - 4 * k + i
        d.line((x0 + 2 * k, y, x1 - 2 * k, y), fill=tuple(int(base[c] + (dark[c] - base[c]) * a * 0.7) for c in range(3)))


def _arrow(d, cx, y, k, col):
    d.polygon([(cx - 4 * k, y), (cx + 4 * k, y), (cx, y + 3 * k)], fill=col)


def gb_cart(k=4, size=(76, 80), label=(11, 23, 53, 46), g=GREY):
    """A Game Boy (or Color) cart in the Shelf's flat style: grey plastic, the
    stepped top right corner, a blank logo recess with grip ridges either
    side, the label, and the arrow under it."""
    w, h = size
    W, H = w * k, h * k
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    step = 7 * k                                     # the top right corner's step
    d.rounded_rectangle((0, 0, W - 1, H - 1), 3 * k, fill=g['edge'])
    d.rounded_rectangle((k, k, W - 1 - k, H - 1 - k), 2 * k, fill=g['base'])
    _shade(d, W, H, k, 0, W - step, g['base'], g['light'], g['dark'])
    d.rectangle((W - step, 0, W - 1, 3 * k - 1), fill=(0, 0, 0, 0))
    d.line((W - step, 3 * k, W - 1 - k, 3 * k), fill=g['edge'], width=k)
    d.line((W - step, 0, W - step, 3 * k), fill=g['edge'], width=k)
    d.line((W - step + k, 4 * k, W - 2 * k, 4 * k), fill=g['light'], width=max(1, k // 2))
    lx, ly, lw, lh = label
    # the logo recess (blank) and the grip ridges
    ry0, ry1 = 5 * k, (ly - 4) * k
    d.rounded_rectangle((15 * k, ry0, W - 15 * k, ry1), (ry1 - ry0) // 2, fill=g['line'])
    d.rounded_rectangle((15 * k, ry0 + k, W - 15 * k, ry1), (ry1 - ry0) // 2, fill=g['well'])
    d.line((17 * k, ry1, W - 17 * k, ry1), fill=g['hi'], width=max(1, k // 2))
    for i in range(3):
        y = ry0 + (2 + 3 * i) * k
        for x0 in (4 * k, W - 12 * k):
            d.line((x0, y, x0 + 8 * k, y), fill=g['line'], width=k)
            d.line((x0, y + k, x0 + 8 * k, y + k), fill=g['hi'], width=max(1, k // 2))
    # the label well and the arrow
    d.rounded_rectangle(((lx - 1) * k, (ly - 1) * k, (lx + lw + 1) * k - 1, (ly + lh + 1) * k - 1), 2 * k, fill=g['well'])
    _arrow(d, (lx + lw / 2) * k, (ly + lh + 3) * k, k, g['well'])
    return im.resize((w, h), Image.LANCZOS)


def nes_cart(k=4, size=(72, 80), label=(30, 1, 35, 55)):
    """An NES cart in the Shelf's flat style: grey plastic, notches along the
    top, a ridged grip panel down the left, the label top right with the
    arrow under it, and the foot a step narrower."""
    w, h = size
    W, H = w * k, h * k
    g = GREY
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    foot = round(h * 0.86) * k
    d.rounded_rectangle((0, 0, W - 1, foot + 2 * k), 2 * k, fill=g['edge'])
    d.rounded_rectangle((2 * k, foot - 2 * k, W - 1 - 2 * k, H - 1), 2 * k, fill=g['edge'])
    d.rounded_rectangle((k, k, W - 1 - k, foot + k), k, fill=g['base'])
    d.rounded_rectangle((3 * k, foot - 2 * k, W - 1 - 3 * k, H - 1 - k), k, fill=g['base'])
    _shade(d, W, H, k, 0, W, g['base'], g['light'], g['dark'])
    for x0 in (3 * k, W - 8 * k):                    # the notches along the top
        d.rectangle((x0, 0, x0 + 5 * k - 1, k - 1), fill=(0, 0, 0, 0))
        d.line((x0, k, x0 + 5 * k - 1, k), fill=g['edge'], width=max(1, k // 2))
    # the grip panel: a raised strip with ridges across it
    gx0, gx1 = 9 * k, 24 * k
    d.rectangle((gx0, 3 * k, gx1, H - 2 * k), fill=g['line'])
    d.rectangle((gx0 + k, 3 * k, gx1 - k, H - 2 * k), fill=g['base'])
    for y in range(5 * k, H - 3 * k, 3 * k):
        d.line((gx0 + k, y, gx1 - k, y), fill=g['line'], width=k)
        d.line((gx0 + k, y + k, gx1 - k, y + k), fill=g['hi'], width=max(1, k // 2))
    d.line((gx1 + 3 * k, 2 * k, gx1 + 3 * k, foot), fill=g['line'], width=max(1, k // 2))
    lx, ly, lw, lh = label
    d.rounded_rectangle(((lx - 1) * k, ly * k, (lx + lw + 1) * k - 1, (ly + lh + 1) * k - 1), k, fill=g['well'])
    _arrow(d, (lx + lw / 2) * k, (ly + lh + 4) * k, k, g['well'])
    d.line((k, foot, 3 * k, foot), fill=g['edge'], width=k)
    d.line((W - 3 * k, foot, W - k, foot), fill=g['edge'], width=k)
    return im.resize((w, h), Image.LANCZOS)


def gg_cart(k=4, size=(80, 76), label=(7, 11, 66, 46)):
    """A Game Gear cart in the Shelf's flat style: black plastic, a hood with
    grip lines across the top and down the sides, the label in its U, an oval
    plaque at the foot."""
    w, h = size
    W, H = w * k, h * k
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    base, dark, light, edge = (64, 64, 72), (22, 22, 26), (124, 124, 136), (10, 10, 12)
    face = (40, 40, 46)
    flange = round(h * 0.38) * k
    # silhouette: a full-width hood, the lower body 1 px in from the sides
    d.rounded_rectangle((0, 0, W - 1, flange + 2 * k), 4 * k, fill=edge)
    d.rounded_rectangle((k, flange - 2 * k, W - 1 - k, H - 1), 3 * k, fill=edge)
    d.rounded_rectangle((k, k, W - 1 - k, flange + k), 3 * k, fill=base)
    d.rounded_rectangle((2 * k, flange - 2 * k, W - 1 - 2 * k, H - 1 - k), 2 * k, fill=base)
    # the top's highlight
    for i in range(3 * k):
        a = 1 - i / (3 * k)
        d.line((4 * k, k + i, W - 4 * k, k + i),
               fill=tuple(int(base[c] + (light[c] - base[c]) * a * 0.8) for c in range(3)))
    # the U the hood leaves: a flat face a step down, round-topped corners
    lx, ly, lw, lh = label
    ux0, ux1, uy = (lx - 3) * k, (lx + lw + 3) * k, (ly - 4) * k
    d.rounded_rectangle((ux0, uy, ux1 - 1, H - 2 * k), 7 * k, fill=dark)          # the step's shadow
    d.rounded_rectangle((ux0 + k, uy + k, ux1 - 1 - k, H - 2 * k), 6 * k, fill=face)
    d.rectangle((2 * k, flange + 2 * k, ux0 + 2 * k, H - 2 * k), fill=face)        # the sides drop
    d.rectangle((ux1 - 2 * k, flange + 2 * k, W - 1 - 2 * k, H - 2 * k), fill=face)
    for x in (2 * k, W - 3 * k):
        d.line((x, flange + 2 * k, x + k, flange + 2 * k), fill=light)
    # grip lines on the hood
    for i in range(3):
        y = int(2.6 * k) + i * k
        d.line((24 * k, y, W - 24 * k, y), fill=(98, 98, 110), width=max(1, k // 2))
    # the label well
    d.rounded_rectangle(((lx - 1) * k, (ly - 1) * k, (lx + lw + 1) * k - 1, (ly + lh + 1) * k - 1),
                        3 * k, fill=(28, 28, 32))
    # the plaque, and the shade along the foot
    py = (ly + lh + 4) * k
    d.rounded_rectangle((W * 0.31, py, W * 0.69, py + 6 * k), 3 * k, fill=(30, 30, 34), outline=(84, 84, 96), width=max(1, k // 2))
    for i in range(3 * k):
        a = i / (3 * k)
        y = H - 2 * k - 3 * k + i
        d.line((3 * k, y, W - 3 * k, y), fill=tuple(int(face[c] + (dark[c] - face[c]) * a * 0.7) for c in range(3)))
    return im.resize((w, h), Image.LANCZOS)


def sms_cart(k=4, size=(96, 61), label=(4, 3, 88, 13)):
    """A Master System cart in the Shelf's flat style: black plastic, the label
    strip across the top, three grooves under it notching both sides, and a
    plain lower body a hair narrower."""
    w, h = size
    W, H = w * k, h * k
    im = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    base, dark, light, edge = (64, 64, 72), (22, 22, 26), (124, 124, 136), (10, 10, 12)
    lx, ly, lw, lh = label
    gy = [(ly + lh + 4 + 2 * i) * k for i in range(3)]          # the grooves' rows
    split = gy[-1] + 2 * k                                      # the lower body starts
    d.rounded_rectangle((0, 0, W - 1, split + 2 * k), 3 * k, fill=edge)
    d.rounded_rectangle((k, split - 2 * k, W - 1 - k, H - 1), 3 * k, fill=edge)
    d.rounded_rectangle((k, k, W - 1 - k, split + k), 2 * k, fill=base)
    d.rounded_rectangle((2 * k, split - 2 * k, W - 1 - 2 * k, H - 1 - k), 2 * k, fill=base)
    for i in range(3 * k):                                      # the top's highlight
        a = 1 - i / (3 * k)
        d.line((3 * k, k + i, W - 3 * k, k + i),
               fill=tuple(int(base[c] + (light[c] - base[c]) * a * 0.8) for c in range(3)))
    # the label well, a shallow step
    d.rounded_rectangle(((lx - 1) * k, (ly - 1) * k, (lx + lw + 1) * k - 1, (ly + lh + 1) * k - 1),
                        k, fill=(28, 28, 32))
    # the grooves, right across and notching the sides
    for y in gy:
        d.rectangle((0, y, W - 1, y + k - 1), fill=edge)
        d.line((k, y + k, W - 1 - k, y + k), fill=(96, 96, 108), width=max(1, k // 2))
    for x in (0, W - k):                                       # the notches show the background
        for y in gy:
            d.rectangle((x, y, x + k - 1, y + k - 1), fill=(0, 0, 0, 0))
    # a soft shade down the lower body
    for i in range(4 * k):
        a = i / (4 * k)
        y = H - 1 - k - 4 * k + i
        d.line((3 * k, y, W - 3 * k, y), fill=tuple(int(base[c] + (dark[c] - base[c]) * a * 0.7) for c in range(3)))
    return im.resize((w, h), Image.LANCZOS)


def hard(im, n):
    """n colours and a hard edge, so the background stays within its palette banks"""
    a = im.getchannel('A').point(lambda v: 255 if v > 128 else 0)
    q = im.convert('RGB').quantize(n, method=Image.MEDIANCUT, dither=Image.NONE).convert('RGBA')
    q.putalpha(a)
    return q

def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    font = font8()
    background(font).save(os.path.join(out, 'background.png'))
    background(font, PLANK_Y_LOW).save(os.path.join(out, 'background_low.png'))
    b, f = console()
    b.save(os.path.join(out, 'console_back.png'))
    f.save(os.path.join(out, 'console_front.png'))
    cart_body().save(os.path.join(out, 'cart.png'))
    gg_cart().save(os.path.join(out, 'gg_shelf.png'))
    gb_cart().save(os.path.join(out, 'gb_shelf.png'))
    gb_cart(label=(11, 23, 53, 47), g=BLACK).save(os.path.join(out, 'gbc_shelf.png'))
    nes_cart().save(os.path.join(out, 'nes_shelf.png'))
    sms_cart().save(os.path.join(out, 'sms_shelf.png'))

main()
