#!/usr/bin/env python3
"""Draw a Sega Master System cart's layers in the style of Slot's hand drawn carts.

    python3 sms.py [--out DIR]

The plastic is a height map lit from the top left with a fine stipple, in 50% grey, so a
shell colour can be blended onto it (the real ones are black); the label well is left flat
grey. Writes, all 400x256:

    sms_cart.png      the outline: white, its alpha the cart's silhouette
    sms_neutral.png   the opaque shell
    sms_clear.png     the see-through shell, with a rim and a sheen
    sms_interior.png  the back plastic a clear cart shows
    sms_board.png     the board a clear cart shows

A wide, plain pak. The label runs across the top, nearly edge to edge, in a shallow well.
Under it the top section ends in three grooves that run right across the face and notch
both sides, and the plain lower body is a hair narrower. No maker's names or marks.
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


# The label, as x, y, w, h: 8:1, as the labels are.
SMS_LABEL = (10, 6, 380, 48)
SMS_LABEL_R = 2
W, H = 400, 256
# The three grooves under the label, as their y centres.
GROOVES = (70, 77, 84)
# Where the lower body starts, and how far in its sides sit.
LOWER_Y, LOWER_IN = 90, 3


def mastersystem(OUT):
    w, h = W, H
    c = Canvas(w, h)
    upper = c.rrect(c.layer(), 0, 0, w, LOWER_Y + 6, 4, corners=(True, True, False, False))
    lower = c.rrect(c.layer(), LOWER_IN, LOWER_Y - 4, w - LOWER_IN, h, 6)
    body = np.maximum(f(upper), f(lower))
    # The grooves notch the sides of the silhouette too.
    notches = c.layer()
    for y in GROOVES:
        c.rrect(notches, -2, y - 1.6, 2.4, y + 1.6, 1)
        c.rrect(notches, w - 2.4, y - 1.6, w + 2, y + 1.6, 1)
    body = body * (1 - f(notches))

    lx, ly, lw, lh = SMS_LABEL
    well = f(c.rrect(c.layer(), lx, ly, lx + lw, ly + lh, SMS_LABEL_R))
    rim_well = f(c.rrect(c.layer(), lx - 2, ly - 2, lx + lw + 2, ly + lh + 2, SMS_LABEL_R + 1))

    grooves = c.layer()
    for y in GROOVES:
        c.rrect(grooves, -4, y - 1.4, w + 4, y + 1.4, 0.8)
    # The step down from the top section to the lower body.
    step = f(c.rrect(c.layer(), -4, LOWER_Y, w + 4, h + 4, 0))

    height = bevel(body, 4.0) * 2.6
    height -= soft(rim_well, 0.7) * 1.2
    height -= soft(f(grooves), 0.35) * 2.4
    height -= soft(step, 0.6) * 1.0
    occl = soft(rim_well, 1.6) * 4 * (1 - well) + soft(f(grooves), 0.6) * 9

    neutral = shade(height, body, well, seed=41, occlusion=occl)
    rgba(neutral, body).save(os.path.join(OUT, "sms_neutral.png"))
    rgba(clear_from(neutral, body, 42), body).save(os.path.join(OUT, "sms_clear.png"))
    rgba(np.full_like(body, 255.0) * body, body).save(os.path.join(OUT, "sms_cart.png"))
    interior(c, body, 43, [(w * 0.12, h * 0.55, 7), (w * 0.88, h * 0.55, 7),
                           (w * 0.5, h * 0.3, 6)]).save(os.path.join(OUT, "sms_interior.png"))

    board_region = c.rrect(c.layer(), 16, LOWER_Y + 6, w - 16, h - 3, 4)
    board = pcb(c, board_region, 44, [(w * 0.18, h * 0.48, w * 0.46, h * 0.68),
                                       (w * 0.56, h * 0.5, w * 0.74, h * 0.64)])
    board, balpha = fingers(c, w * 0.1, w * 0.9, h * 0.9, h - 4, 25, board,
                            f(board_region))
    rgba_colour(board, balpha).save(os.path.join(OUT, "sms_board.png"))


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", default=".")
    out = ap.parse_args().out
    os.makedirs(out, exist_ok=True)
    mastersystem(out)
    print("sms_cart/neutral/clear/interior/board.png in", out)
