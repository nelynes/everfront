#!/usr/bin/env python3
"""Draw a Game Gear cart's layers in the style of Slot's hand drawn carts.

    python3 gg.py [--out DIR]

The plastic is a height map lit from the top left with a fine stipple, in 50% grey, so a
shell colour can be blended onto it; the label well is left flat grey. Writes, all 352x329:

    gg_cart.png      the outline: white, its alpha the cart's silhouette
    gg_neutral.png   the opaque shell
    gg_clear.png     the see-through shell, with a rim and a sheen
    gg_interior.png  the back plastic a clear cart shows
    gg_board.png     the board a clear cart shows

A nearly square pak: a raised hood with three grip ridges runs across the top and down
both sides as far as the flanges, the label sits in the U it makes, and a blank oval
plaque sits at the foot. The label is x 31, y 48, 290 x 200, with 14 px corners.
No maker's names or marks.
"""

import os
import sys

import numpy as np
from PIL import Image, ImageDraw
from scipy import ndimage


S = 4  # supersampling
NEUTRAL = 128.0
WELL = 119
LIGHT = np.array([-0.62, -0.78])  # from the top left


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.W, self.H = w * S, h * S

    def layer(self):
        return Image.new("L", (self.W, self.H), 0)

    def rrect(self, img, x0, y0, x1, y1, r, fill=255, corners=None):
        d = ImageDraw.Draw(img)
        box = [x0 * S, y0 * S, x1 * S - 1, y1 * S - 1]
        if corners is None:
            d.rounded_rectangle(box, radius=r * S, fill=fill)
        else:
            d.rounded_rectangle(box, radius=r * S, fill=fill, corners=corners)
        return img

    def poly(self, img, pts, fill=255):
        ImageDraw.Draw(img).polygon([(x * S, y * S) for x, y in pts], fill=fill)
        return img

    def ellipse(self, img, x0, y0, x1, y1, fill=255, width=0):
        d = ImageDraw.Draw(img)
        if width:
            d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], outline=fill, width=width * S)
        else:
            d.ellipse([x0 * S, y0 * S, x1 * S, y1 * S], fill=fill)
        return img



def f(img):
    return np.asarray(img, dtype=np.float32) / 255.0


def soft(mask, px):
    return ndimage.gaussian_filter(mask, px * S) if px else mask


def down(a):
    """Box-filter a supersampled array back to 1x."""
    h, w = a.shape[0] // S, a.shape[1] // S
    return a.reshape(h, S, w, S, *a.shape[2:]).mean(axis=(1, 3))


def bevel(mask, width):
    """A rounded shoulder rising `width` pixels in from the mask's edge."""
    dist = ndimage.distance_transform_edt(mask > 0.5) / S
    t = np.clip(dist / width, 0.0, 1.0)
    return np.sin(t * np.pi / 2)


def light(height, gain):
    gy, gx = np.gradient(height, 1.0 / S)
    return (gx * LIGHT[0] + gy * LIGHT[1]) * gain


def grain(shape, seed, std=5.0):
    rng = np.random.default_rng(seed)
    n = rng.normal(0.0, std, (shape[0] // S, shape[1] // S)).astype(np.float32)
    n = ndimage.gaussian_filter(n, 0.42)
    n *= std / max(n.std(), 1e-6)
    return np.kron(n, np.ones((S, S), dtype=np.float32))


def rim(body):
    """The moulded edge the hand drawn carts have: two pixels bright where the
    edge faces up, a little dark down the sides and dark along the bottom."""
    dist = ndimage.distance_transform_edt(body > 0.5) / S
    band = np.clip(2.2 - dist, 0, 1)
    smooth = ndimage.gaussian_filter(body, 2.0 * S)
    gy, gx = np.gradient(smooth)
    norm = np.hypot(gx, gy) + 1e-6
    nx, ny = -gx / norm, -gy / norm  # outward
    up = np.clip(-ny, 0, 1)
    down = np.clip(ny, 0, 1)
    return band * (60 * up - 25 * down - 20 * np.abs(nx))


def shade(height, body, well, seed, gain=26.0, occlusion=None, smooth=None):
    """Light the plastic, stipple it, and leave the label well flat. Where
    `smooth` is set the stipple fades out, as on a polished recess floor."""
    g = NEUTRAL + light(height, gain)
    if occlusion is not None:
        g -= occlusion
    g += rim(body)
    stipple = grain(height.shape, seed)
    if smooth is not None:
        stipple *= 1.0 - 0.85 * smooth
    g += stipple
    g = np.where(well > 0.5, WELL, g)
    return np.clip(g, 0, 255) * body


def rgba(grey, alpha):
    g = down(grey / np.maximum(alpha, 1e-3) * (alpha > 0))
    a = down(alpha)
    out = np.dstack([g, g, g, a * 255.0])
    return Image.fromarray(np.clip(out, 0, 255).round().astype(np.uint8), "RGBA")


def rgba_colour(rgb, alpha):
    a = down(alpha)
    c = down(rgb * alpha[..., None]) / np.maximum(a[..., None], 1e-3)
    out = np.dstack([c, a * 255.0])
    return Image.fromarray(np.clip(out, 0, 255).round().astype(np.uint8), "RGBA")


def clear_from(grey, body, seed):
    """The see-through shell: flatter plastic, a darker rim, a diagonal sheen."""
    h, w = body.shape
    dist = ndimage.distance_transform_edt(body > 0.5) / S
    rim = dist < 5
    ys, xs = np.mgrid[0:h, 0:w] / S
    sheen = np.abs(xs + ys - (w / S) / 3) < (w / S) / 14
    g = NEUTRAL + (grey - NEUTRAL) * 0.55
    g = np.where(sheen, g + 24, g)
    g = np.where(rim, g - 28, g)
    return np.clip(g, 0, 255) * body


def interior(c, body, seed, bosses):
    """The back plastic: flat grey with its moulded screw bosses and ribs."""
    height = bevel(body, 4.0) * 2.0
    for (cx, cy, r) in bosses:
        ring = c.ellipse(c.layer(), cx - r, cy - r, cx + r, cy + r)
        hole = c.ellipse(c.layer(), cx - r * 0.45, cy - r * 0.45, cx + r * 0.45, cy + r * 0.45)
        height += soft(f(ring), 0.6) * 2.0 - soft(f(hole), 0.4) * 3.0
    g = NEUTRAL - 6 + light(height, 24.0) + grain(height.shape, seed, 4.0)
    return rgba(np.clip(g, 0, 255) * body, body)


def pcb(c, region, seed, chips):
    """A green board with traces and chips, inside `region`."""
    h, w = c.H, c.W
    rng = np.random.default_rng(seed)
    base = np.array([31, 92, 58], dtype=np.float32)
    rgb = np.ones((h, w, 3), np.float32) * base
    rgb += grain((h, w), seed + 1, 5.0)[..., None]
    traces = c.layer()
    d = ImageDraw.Draw(traces)
    for _ in range(26):
        x = rng.uniform(0.1, 0.9) * c.w
        y = rng.uniform(0.15, 0.7) * c.h
        pts = [(x, y)]
        for _ in range(3):
            if rng.random() < 0.5:
                x += rng.uniform(-40, 40)
            else:
                y += rng.uniform(-30, 30)
            pts.append((x, y))
        d.line([(px * S, py * S) for px, py in pts], fill=255, width=int(1.2 * S))
    t = f(traces)[..., None]
    rgb = rgb * (1 - t) + np.array([52, 128, 80], np.float32) * t
    for (x0, y0, x1, y1) in chips:
        chip = f(c.rrect(c.layer(), x0, y0, x1, y1, 1.5))[..., None]
        rgb = rgb * (1 - chip) + np.array([22, 26, 34], np.float32) * chip
        leg = c.layer()
        for x in np.arange(x0 + 2, x1 - 1, 3.0):
            c.rrect(leg, x, y0 - 2, x + 1.4, y0, 0.3)
            c.rrect(leg, x, y1, x + 1.4, y1 + 2, 0.3)
        lg = f(leg)[..., None]
        rgb = rgb * (1 - lg) + np.array([196, 196, 190], np.float32) * lg
    return rgb * f(region)[..., None]


def fingers(c, x0, x1, y0, y1, count, rgb, alpha, width=0.55):
    """Gold edge contacts, `count` of them spread from x0 to x1."""
    pitch = (x1 - x0) / count
    gold = c.layer()
    for k in range(count):
        x = x0 + pitch * (k + (1 - width) / 2)
        c.rrect(gold, x, y0, x + pitch * width, y1, 0.4)
    g = f(gold)
    shine = np.linspace(1.08, 0.86, c.H)[:, None]
    colour = np.array([208, 164, 72], np.float32) * shine[..., None]
    rgb = rgb * (1 - g[..., None]) + colour * g[..., None]
    return rgb, np.maximum(alpha, g)


# --------------------------------------------------------------------------
# WonderSwan: a wide shell with an oval nameplate, and the board's contacts
# showing below its shoulder.




GG_LABEL_R = 14  # the label's corner radius, as slot-ui rounds the art


def gamegear(OUT):
    w, h = 352, 329
    c = Canvas(w, h)
    flange = round(h * 0.376)
    inset = 4
    upper = c.rrect(c.layer(), 0, 0, w, flange + 6, 15, corners=(True, True, False, False))
    lower = c.rrect(c.layer(), inset, flange - 6, w - inset, h, 8)
    body = np.maximum(f(upper), f(lower))

    lx0, ly0 = round(w * 0.088), round(h * 0.145)
    lx1, ly1 = round(w * 0.912), round(h * 0.755)
    well = f(c.rrect(c.layer(), lx0, ly0, lx1, ly1, GG_LABEL_R))

    # The U the hood leaves open: the label well and the face around it,
    # from just under the grip band down to the foot.
    side = round(w * 0.058)
    band = round(h * 0.118)
    # Its top corners are big, slightly tall arches, as on the real pak.
    rx, ry = 42, 50
    face = c.layer()
    d = ImageDraw.Draw(face)
    d.rectangle([(side + rx) * S, band * S, (w - side - rx) * S, (h + 20) * S], fill=255)
    d.rectangle([side * S, (band + ry) * S, (w - side) * S, (h + 20) * S], fill=255)
    for cx in (side, w - side - 2 * rx):
        d.ellipse([cx * S, band * S, (cx + 2 * rx) * S, (band + 2 * ry) * S], fill=255)
    # Below the flanges the sides drop to the face too.
    c.rrect(face, 0, flange + 3, w, h + 20, 0)
    face = f(face)

    grips = c.layer()
    for y in (0.064, 0.083, 0.102):
        c.rrect(grips, w * 0.30, h * y, w * 0.70, h * y + 2.2, 1.1)
    plate = c.rrect(c.layer(), w * 0.305, h * 0.805, w * 0.695, h * 0.925, h * 0.06)

    height = bevel(body, 4.5) * 2.8
    height += (1 - soft(face, 0.7)) * 3.4
    height += soft(f(grips), 0.4) * 1.2
    height -= soft(well, 0.8) * 1.3
    height -= soft(f(plate), 0.7) * 2.6
    occl = soft(well, 2.0) * 5 + soft(f(plate), 1.5) * 7 + soft(face, 2.5) * 3 * (1 - well)

    neutral = shade(height, body, well, seed=31, occlusion=occl)
    rgba(neutral, body).save(os.path.join(OUT, "gg_neutral.png"))
    rgba(clear_from(neutral, body, 32), body).save(os.path.join(OUT, "gg_clear.png"))
    rgba(np.full_like(body, 255.0) * body, body).save(os.path.join(OUT, "gg_cart.png"))
    interior(c, body, 33, [(w * 0.5, h * 0.2, 9), (w * 0.14, h * 0.6, 6),
                           (w * 0.86, h * 0.6, 6)]).save(os.path.join(OUT, "gg_interior.png"))

    board_region = c.rrect(c.layer(), 12, h * 0.16, w - 12, h - 3, 5)
    board = pcb(c, board_region, 34, [(w * 0.2, h * 0.3, w * 0.5, h * 0.55),
                                       (w * 0.6, h * 0.36, w * 0.78, h * 0.5)])
    board, balpha = fingers(c, w * 0.08, w * 0.92, h * 0.94, h - 5, 50, board,
                            f(board_region))
    rgba_colour(board, balpha).save(os.path.join(OUT, "gg_board.png"))


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=".")
    out = ap.parse_args().out
    os.makedirs(out, exist_ok=True)
    gamegear(out)
    print("gg_cart/neutral/clear/interior/board.png in", out)
