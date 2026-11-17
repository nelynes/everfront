#!/usr/bin/env python3
"""Art for the Art Book theme, inspired by Art Book Next's outline style
(github.com/anthonycaccese/art-book-next-es): a black page, a slanted panel in each
system's colour standing behind the selected cart like a book cover, and a white line
drawing of the system at the panel's foot, cropped by its edges. All of it is drawn
here, pixel by pixel, at the GBA's own resolution.

The carts are outlines too: black bodies with a white edge. Their sizes and label wells
are the Shelf theme's, so the Shelf label packs work here unchanged.

    python3 make_art.py <out dir>
"""
import os
import sys
from PIL import Image, ImageDraw

SW, SH = 240, 160
TOP = 16                 # the header: system name and clock on black
FOOT = 100               # carts stand here
LIP = 145                # the slot line; the cart goes in below it, the game's name sits under it

WHITE = (236, 236, 240)
GREY = (96, 96, 104)
BODY = (18, 18, 22)
WELL = (44, 44, 50)
CLEAR = (0, 0, 0, 0)

# Each system's panel, in the spirit of Art Book Next's colours.
PANELS = {
    'gba': (74, 104, 212),
    'gb': (148, 32, 98),
    'gbc': (230, 174, 38),
    'nes': (188, 46, 58),
    'sms': (206, 74, 50),
    'gg': (36, 134, 178),
}


def panel_left(y):
    """The panel's left edge at row y: it leans right, one pixel every four rows."""
    return 48 + (SH - y) // 4


PANEL_W = 104


def panel_mask():
    m = Image.new('L', (SW, SH), 0)
    d = ImageDraw.Draw(m)
    for y in range(TOP, SH):
        x = panel_left(y)
        d.line((x, y, x + PANEL_W - 1, y), fill=255)
    return m


# ---------------------------------------------------------------- line drawings
# Plain line icons of each system: what it is, not a copy of the real thing.

def _dpad(d, cx, cy, r=4, col=WHITE):
    a = r // 2
    d.line([(cx - a, cy - r), (cx + a, cy - r), (cx + a, cy - a), (cx + r, cy - a), (cx + r, cy + a),
            (cx + a, cy + a), (cx + a, cy + r), (cx - a, cy + r), (cx - a, cy + a), (cx - r, cy + a),
            (cx - r, cy - a), (cx - a, cy - a), (cx - a, cy - r)], fill=col)


def _ring(d, cx, cy, r, col=WHITE):
    d.ellipse((cx - r, cy - r, cx + r, cy + r), outline=col)


def draw_wide_handheld(d, x, y, shoulders=True):
    """A wide handheld: screen in the middle, d-pad left, two buttons right."""
    w, h = 84, 46
    if shoulders:
        d.rounded_rectangle((x + 4, y - 3, x + 22, y + 6), 3, outline=WHITE)
        d.rounded_rectangle((x + w - 22, y - 3, x + w - 4, y + 6), 3, outline=WHITE)
    d.rounded_rectangle((x, y, x + w, y + h), 12, outline=WHITE, fill=None)
    d.rectangle((x + 25, y + 8, x + w - 25, y + 34), outline=WHITE)
    d.rectangle((x + 29, y + 12, x + w - 29, y + 30), outline=WHITE)
    _dpad(d, x + 13, y + 21)
    _ring(d, x + w - 16, y + 24, 3)
    _ring(d, x + w - 9, y + 18, 3)
    d.line((x + 32, y + 39, x + 36, y + 39), fill=WHITE)
    d.line((x + w - 36, y + 39, x + w - 32, y + 39), fill=WHITE)


def draw_tall_handheld(d, x, y, colour_lines=False):
    """An upright handheld: screen above, d-pad and two buttons below, speaker slots."""
    w, h = 52, 84
    d.rounded_rectangle((x, y, x + w, y + h), 4, outline=WHITE)
    d.line((x + 1, y + 6, x + w - 1, y + 6), fill=WHITE)
    d.rounded_rectangle((x + 5, y + 10, x + w - 5, y + 42), 3, outline=WHITE)
    d.rectangle((x + 12, y + 15, x + w - 12, y + 35), outline=WHITE)
    if colour_lines:
        for i in range(3):
            d.line((x + 7, y + 38 - 2 * i, x + 10, y + 38 - 2 * i), fill=WHITE)
    _dpad(d, x + 12, y + 56)
    _ring(d, x + w - 17, y + 58, 3)
    _ring(d, x + w - 9, y + 54, 3)
    for i in range(3):
        d.line((x + w - 14 + 3 * i, y + 72, x + w - 18 + 3 * i, y + 78), fill=WHITE)


def draw_controller(d, x, y, pills=True):
    """A rectangular pad: d-pad left, two round buttons right, select/start between."""
    w, h = 84, 36
    d.rounded_rectangle((x, y, x + w, y + h), 4, outline=WHITE)
    d.line((x + 34, y - 6, x + 34, y), fill=WHITE)            # the cord
    d.line((x + 34, y - 6, x + 46, y - 12), fill=WHITE)
    _dpad(d, x + 15, y + 19, r=6)
    if pills:
        d.rounded_rectangle((x + 32, y + 21, x + 39, y + 24), 1, outline=WHITE)
        d.rounded_rectangle((x + 43, y + 21, x + 50, y + 24), 1, outline=WHITE)
    _ring(d, x + w - 24, y + 22, 4)
    _ring(d, x + w - 12, y + 22, 4)


DRAWINGS = {
    'gba': lambda d: draw_wide_handheld(d, 92, 106),
    'gb': lambda d: draw_tall_handheld(d, 110, 104),
    'gbc': lambda d: draw_tall_handheld(d, 110, 104, colour_lines=True),
    'nes': lambda d: draw_controller(d, 96, 114),
    'sms': lambda d: draw_controller(d, 100, 114, pills=False),
    'gg': lambda d: draw_wide_handheld(d, 90, 106, shoulders=False),
}


def background(system):
    im = Image.new('RGB', (SW, SH), (0, 0, 0))
    mask = panel_mask()
    im.paste(PANELS[system], (0, 0), mask)
    art = Image.new('RGBA', (SW, SH), CLEAR)
    DRAWINGS[system](ImageDraw.Draw(art))
    # cropped by the panel's edges, as Art Book Next crops its art
    a = art.getchannel('A')
    a.paste(0, (0, 0), Image.eval(mask, lambda v: 255 - v))
    art.putalpha(a)
    im.paste(art.convert('RGB'), (0, 0), art.getchannel('A'))
    return im


def slot():
    """The slot: a white line across the bottom, black below it. The cart goes in behind it."""
    back = Image.new('RGBA', (SW, 34), CLEAR)
    front = Image.new('RGBA', (SW, 34), CLEAR)
    d = ImageDraw.Draw(front)
    top = LIP - (SH - 34)
    d.rectangle((0, top, SW - 1, 33), fill=(0, 0, 0, 255))
    d.line((0, top, SW - 1, top), fill=WHITE + (255,))
    d.line((88, top, 151, top), fill=(0, 0, 0, 255))          # the slot's mouth
    d.line((88, top, 88, top + 3), fill=WHITE + (255,))
    d.line((151, top, 151, top + 3), fill=WHITE + (255,))
    return back, front


# ---------------------------------------------------------------- outline carts

def _new(w, h):
    im = Image.new('RGBA', (w, h), CLEAR)
    return im, ImageDraw.Draw(im)


def _well(d, label, r=1):
    lx, ly, lw, lh = label
    d.rounded_rectangle((lx - 1, ly - 1, lx + lw, ly + lh), r, fill=WELL, outline=GREY)


def _arrow(d, cx, y):
    d.polygon([(cx - 3, y), (cx + 3, y), (cx, y + 3)], outline=GREY)


def gba_cart(size=(96, 58), label=(8, 6, 80, 41)):
    w, h = size
    im, d = _new(w, h)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 5, fill=BODY, outline=WHITE)
    d.line((4, 3, w - 5, 3), fill=GREY)                     # the top's ridge
    _well(d, label)
    lx, ly, lw, lh = label
    d.line((w // 2 - 6, ly + lh + 4, w // 2 + 6, ly + lh + 4), fill=GREY)
    return im


def gb_cart(size=(76, 80), label=(11, 23, 53, 46)):
    w, h = size
    im, d = _new(w, h)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 3, fill=BODY, outline=WHITE)
    # the stepped top right corner
    d.rectangle((w - 8, 0, w - 1, 2), fill=CLEAR)
    d.line((w - 8, 0, w - 8, 3), fill=WHITE)
    d.line((w - 8, 3, w - 1, 3), fill=WHITE)
    lx, ly, lw, lh = label
    d.rounded_rectangle((15, 6, w - 16, ly - 5), 5, outline=GREY)    # the logo recess
    for i in range(3):
        d.line((4, 8 + 3 * i, 11, 8 + 3 * i), fill=GREY)
        d.line((w - 12, 8 + 3 * i, w - 5, 8 + 3 * i), fill=GREY)
    _well(d, label, 2)
    _arrow(d, lx + lw // 2, ly + lh + 3)
    return im


def nes_cart(size=(72, 80), label=(30, 1, 35, 55)):
    w, h = size
    foot = round(h * 0.86)
    im, d = _new(w, h)
    d.rounded_rectangle((0, 0, w - 1, foot + 1), 2, fill=BODY, outline=WHITE)
    d.rounded_rectangle((2, foot - 1, w - 3, h - 1), 2, fill=BODY, outline=WHITE)
    d.line((1, foot - 1, w - 2, foot - 1), fill=BODY)
    for x0 in (3, w - 8):
        d.line((x0, 0, x0 + 4, 0), fill=CLEAR)
    for y in range(4, h - 3, 3):
        d.line((10, y, 23, y), fill=GREY)
    d.line((26, 2, 26, foot - 2), fill=GREY)
    lx, ly, lw, lh = label
    _well(d, label)
    _arrow(d, lx + lw // 2, ly + lh + 4)
    return im


def sms_cart(size=(96, 61), label=(4, 3, 88, 13)):
    w, h = size
    im, d = _new(w, h)
    lx, ly, lw, lh = label
    split = ly + lh + 10
    d.rounded_rectangle((0, 0, w - 1, split + 1), 3, fill=BODY, outline=WHITE)
    d.rounded_rectangle((1, split - 1, w - 2, h - 1), 3, fill=BODY, outline=WHITE)
    d.line((1, split - 1, w - 2, split - 1), fill=BODY)
    for i in range(3):
        y = ly + lh + 4 + 2 * i
        d.line((2, y, w - 3, y), fill=GREY)
        d.point((0, y), fill=CLEAR)
        d.point((w - 1, y), fill=CLEAR)
    _well(d, label)
    return im


def gg_cart(size=(80, 76), label=(7, 11, 66, 46)):
    w, h = size
    im, d = _new(w, h)
    d.rounded_rectangle((0, 0, w - 1, h - 1), 4, fill=BODY, outline=WHITE)
    lx, ly, lw, lh = label
    d.rounded_rectangle((lx - 3, ly - 4, lx + lw + 2, h - 3), 6, outline=GREY)   # the hood's U
    for i in range(3):
        d.line((24, 3 + 2 * i, w - 25, 3 + 2 * i), fill=GREY)
    _well(d, label)
    py = ly + lh + 4
    d.rounded_rectangle((w * 0.31, py, w * 0.69, py + 6), 3, outline=GREY)
    return im


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), 'art')
    os.makedirs(out, exist_ok=True)
    for s in PANELS:
        background(s).save(os.path.join(out, f'background_{s}.png'))
    b, f = slot()
    b.save(os.path.join(out, 'slot_back.png'))
    f.save(os.path.join(out, 'slot_front.png'))
    gba_cart().save(os.path.join(out, 'cart_gba.png'))
    gb_cart().save(os.path.join(out, 'cart_gb.png'))
    gb_cart(label=(11, 23, 53, 47)).save(os.path.join(out, 'cart_gbc.png'))
    nes_cart().save(os.path.join(out, 'cart_nes.png'))
    sms_cart().save(os.path.join(out, 'cart_sms.png'))
    gg_cart().save(os.path.join(out, 'cart_gg.png'))


if __name__ == '__main__':
    main()
