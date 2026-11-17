#!/usr/bin/env python3
"""Builds an EverFront theme file (.thm) from a theme folder.

usage: make_theme.py <theme.json> <art dir> <out.thm> [preview.png]

A theme folder holds theme.json (which art files, at what size, and the
layout numbers); the art is read from <art dir> and scaled to the sizes given.

.thm (little endian):
  0   "EFTH"  u16 version (1)  u16 number of params
  8   name[24]            NUL-padded
  32  labels[24]          label pack file in everfront/
  56  5 x {u32 offset, u32 size}: palette, tiles4, map_back, map_front, cart
  96  s16 params[]        (PARAMS below, in that order)
  then the sections, each 4-byte aligned:
  palette    u16 BGR555 x up to 112 (entries 112-255 are the label slots)
  tiles4     4bpp tiles, 32 bytes each (tile 0 blank)
  map_back   30x20 u16 entries for BG3 (tile | bank << 12)
  map_front  30x20 u16 entries for BG0 (0 = nothing)
  cart       cart_w x cart_h bytes, 8bpp palette indices, 0 = clear
Palette contract with the engine: 0-63 background (4 banks), 64-79 bar
front (bank 4; 15 = clock colour), 80-95 text (bank 5; 1 = name colour),
96-111 cart plastic.
"""
import warnings; warnings.filterwarnings("ignore")
import json, os, random, struct, sys
from PIL import Image

PARAMS = ['cart_w', 'cart_h', 'label_x', 'label_y', 'label_w', 'label_h', 'label_cols',
          'foot_y', 'pitch', 'side_permille', 'omega_x16', 'name_row', 'name_bank',
          'name_colour', 'clock_row', 'clock_col', 'clock_bank', 'clock_colour',
          'seat_w', 'seat_h', 'lip_x16', 'seated_foot_x16', 'seat_frames', 'eject_frames',
          'lift', 'side_dim', 'sys_off_lo', 'sys_off_hi', 'sys_count', 'sys_row', 'sys_col']

def third(path, size):
    return Image.open(path).convert('RGBA').resize(size, Image.LANCZOS)

def bgr555(c):
    r, g, b = c[:3]
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)

def quant(rgb, n):
    q = rgb.quantize(n, method=Image.MEDIANCUT, dither=Image.NONE)
    pal = q.getpalette()[:n * 3]
    pal = pal + [0] * (n * 3 - len(pal))
    return [tuple(pal[i * 3:i * 3 + 3]) for i in range(n)], list(q.getdata())

def tiles_of(img_idx, w, h):
    for ty in range(h // 8):
        for tx in range(w // 8):
            yield tx, ty, tuple(img_idx[(ty * 8 + y) * w + tx * 8 + x] for y in range(8) for x in range(8))

def exact_banks(rgb, banks):
    """When the picture has few colours: put each tile in a bank that holds all
    of its colours exactly (greedy, biggest tiles first). None if it can't."""
    w, h = rgb.size
    px = rgb.load()
    tiles = {}
    for ty in range(h // 8):
        for tx in range(w // 8):
            tiles[(tx, ty)] = [px[tx * 8 + x, ty * 8 + y] for y in range(8) for x in range(8)]
    sets = [set() for _ in range(banks)]
    where = {}
    for t in sorted(tiles, key=lambda t: -len(set(tiles[t]))):
        cs = set(tiles[t])
        best = None
        for b in range(banks):
            n = len(sets[b] | cs)
            if n <= 15 and (best is None or n - len(sets[b]) < best[0]):
                best = (n - len(sets[b]), b)
        if best is None:
            return None
        sets[best[1]] |= cs
        where[t] = best[1]
    pals = []
    for b in range(banks):
        cols = sorted(sets[b])
        pals.append([(0, 0, 0)] + cols + [(0, 0, 0)] * (15 - len(cols)))
    out = {}
    for t, b in where.items():
        idx = {c: i + 1 for i, c in enumerate(sorted(sets[b]))}
        out[t] = (b, tuple(idx[c] for c in tiles[t]))
    return pals, out

def multibank(rgb, banks):
    r = exact_banks(rgb, banks) if len(set(rgb.getdata())) <= 15 * banks else None
    if r:
        return r
    """-> (palettes [banks][16], per-tile (bank, 64 indices 1..15))"""
    w, h = rgb.size
    coords = [(tx, ty) for ty in range(h // 8) for tx in range(w // 8)]
    def mean(t):
        return rgb.crop((t[0] * 8, t[1] * 8, t[0] * 8 + 8, t[1] * 8 + 8)).resize((1, 1), Image.BOX).getpixel((0, 0))
    means = {t: mean(t) for t in coords}
    random.seed(1)
    cent = [means[t] for t in random.sample(coords, banks)]
    for _ in range(15):
        groups = [[] for _ in range(banks)]
        for t in coords:
            k = min(range(banks), key=lambda i: sum((a - b) ** 2 for a, b in zip(means[t], cent[i])))
            groups[k].append(t)
        cent = [tuple(sum(means[t][c] for t in g) / len(g) for c in range(3)) if g else cent[i]
                for i, g in enumerate(groups)]
    pals, tiles = [], {}
    for b, g in enumerate(groups):
        strip = Image.new('RGB', (8 * max(1, len(g)), 8))
        for i, t in enumerate(g):
            strip.paste(rgb.crop((t[0] * 8, t[1] * 8, t[0] * 8 + 8, t[1] * 8 + 8)), (i * 8, 0))
        cols, idx = quant(strip, 15)
        pals.append([(0, 0, 0)] + cols + [(0, 0, 0)] * (15 - len(cols)))
        sw = strip.width
        for i, t in enumerate(g):
            tiles[t] = (b, tuple(1 + idx[y * sw + i * 8 + x] for y in range(8) for x in range(8)))
    return pals, tiles

def pack4(t):
    out = bytearray()
    for y in range(8):
        row = 0
        for x in range(8):
            row |= (t[y * 8 + x] & 15) << (4 * x)
        out += struct.pack('<I', row)
    return out


def main():
    tj, art, out = sys.argv[1], sys.argv[2], sys.argv[3]
    preview = sys.argv[4] if len(sys.argv) > 4 else None
    t = json.load(open(tj))
    lay = t['layout']
    SW, SH = t['background']['size']
    CW, CH = t['cart']['size']
    TEXT = tuple(t['text_colour'])
    pal = [(0, 0, 0)] * 112
    tiles, tindex = [tuple([0] * 64)], {}
    tindex[tiles[0]] = 0

    def add(tl):
        if tl not in tindex:
            tindex[tl] = len(tiles)
            tiles.append(tl)
        return tindex[tl]

    def asset(key):
        a = t[key]
        return third(os.path.join(art, a['file']), tuple(a['size'])), tuple(a.get('at', (0, 0)))

    # background + back of the bar: 4 banks
    bg, _ = asset('background')
    back, at = asset('bar_back')
    bg.alpha_composite(back, at)
    # systems with a background of their own (a lower shelf for taller carts)
    # share the 4 banks: all the backgrounds are banked as one tall picture
    alt_bgs = []
    for ext, sd in t.get('systems', {}).items():
        if 'background' in sd:
            abg = third(os.path.join(art, sd['background']), (SW, SH))
            abg.alpha_composite(back, at)
            alt_bgs.append(ext)
            sd['_bg'] = abg
    tall = Image.new('RGBA', (SW, SH * (1 + len(alt_bgs))))
    tall.paste(bg, (0, 0))
    for k, ext in enumerate(alt_bgs):
        tall.paste(t['systems'][ext].pop('_bg'), (0, SH * (1 + k)))
    pals, wt = multibank(tall.convert('RGB'), 4)
    for b in range(4):
        for i in range(16):
            pal[b * 16 + i] = pals[b][i]
    pal[0] = pals[0][1]
    def back_map(k):
        m = []
        for ty in range(20):
            for tx in range(30):
                b, tl = wt[(tx, ty + 20 * k)]
                m.append(add(tl) | (b << 12))
        return m
    map_back = back_map(0)
    alt_maps = {ext: back_map(1 + k) for k, ext in enumerate(alt_bgs)}

    # front of the bar: bank 4, colour 15 kept for the clock
    front = Image.new('RGBA', (SW, SH), (0, 0, 0, 0))
    fimg, at = asset('bar_front')
    front.alpha_composite(fimg, at)
    a = [v > 128 for v in front.getchannel('A').getdata()]
    rgb = front.convert('RGB')
    opaque = Image.new('RGB', (sum(a), 1))
    opaque.putdata([p for p, m in zip(rgb.getdata(), a) if m])
    on_bar = bool(lay.get('name_on_bar'))           # name drawn on the bar: bank 4, colour 14 is the text
    fcols, _ = quant(opaque, 13 if on_bar else 14)
    pq = Image.new('P', (1, 1))
    pq.putpalette((sum((list(c) for c in fcols), []) * (256 // len(fcols) + 1))[:768])
    fidx = list(rgb.quantize(palette=pq, dither=Image.NONE).getdata())
    fidx = [1 + v % len(fcols) if m else 0 for v, m in zip(fidx, a)]
    assert max(fidx) <= 15, max(fidx)
    for i, c in enumerate(fcols):
        pal[64 + 1 + i] = c
    pal[64 + 15] = tuple(t.get('clock_colour', TEXT))
    if on_bar:
        pal[64 + 14] = TEXT
    map_front = []
    for tx, ty, tl in tiles_of(fidx, SW, SH):
        map_front.append(add(tl) | (4 << 12) if any(tl) else 0)

    # text colours (bank 5)
    pal[80 + 1] = TEXT
    pal[80 + 2] = tuple(t.get('text_dim', TEXT))

    # cart plastic: 96..111 (0 = clear)
    def cart_of(img):
        ba = [v > 128 for v in img.getchannel('A').getdata()]
        brgb = img.convert('RGB')
        bop = Image.new('RGB', (sum(ba), 1))
        bop.putdata([p for p, m in zip(brgb.getdata(), ba) if m])
        ccols, _ = quant(bop, 15)
        cq = Image.new('P', (1, 1))
        cq.putpalette((sum((list(c) for c in ccols), []) * (256 // len(ccols) + 1))[:768])  # padding repeats the real colours
        cidx = list(brgb.quantize(palette=cq, dither=Image.NONE).getdata())
        return [97 + v % len(ccols) if m else 0 for v, m in zip(cidx, ba)], ccols   # never past them

    body, _ = asset('cart')
    cart, ccols = cart_of(body)
    for i, c in enumerate(ccols):
        pal[97 + i] = c

    # other systems' carts (GB, GBC, NES...): each its own picture, label well
    # and 15 plastic colours, swapped in when the shelf changes system
    systems = []
    for ext, sd in t.get('systems', {}).items():
        base = os.path.dirname(os.path.abspath(tj)) if sd.get('local') else art
        if sd.get('slot_art'):                        # Slot's own cart art (credited), $SLOT_ASSETS
            base = os.environ.get('SLOT_ASSETS', '../nelynes/slotPlus/assets')
        img = third(os.path.join(base, sd['file']), tuple(sd['size']))
        sc, scols = cart_of(img)
        systems.append((ext, sd, sc, scols))

    lx, ly, lw, lh = lay['label']
    p = dict(cart_w=CW, cart_h=CH, label_x=lx, label_y=ly, label_w=lw, label_h=lh,
             label_cols=lay['label_colours'], foot_y=lay['foot_y'], pitch=lay['pitch'],
             side_permille=round(lay['side_scale'] * 1000), omega_x16=round(lay['spring_omega'] * 16),
             name_row=lay['name_row'], name_bank=4 if on_bar else 5, name_colour=14 if on_bar else 1,
             clock_row=lay['clock_row'], clock_col=lay['clock_col'], clock_bank=4, clock_colour=15,
             seat_w=lay['seated_size'][0], seat_h=lay['seated_size'][1],
             lip_x16=round(lay['lip_y'] * 16), seated_foot_x16=round(lay['seated_foot_y'] * 16),
             seat_frames=lay['seat_frames'], eject_frames=lay['eject_frames'],
             lift=lay.get('lift', 0), side_dim=lay.get('side_dim', 0),
             sys_off_lo=0, sys_off_hi=0, sys_count=len(systems),
             sys_row=lay.get('system_name', [-1, 0])[0], sys_col=lay.get('system_name', [-1, 0])[1])
    params = [p[k] for k in PARAMS]

    # what the engine can take (checked again on the GBA)
    # (the same rules as theme_check() in main.c)
    lift = lay.get('lift', 0)
    def check(what, cw, chh, pitch, foot):
        assert cw % 4 == 0 and cw <= pitch and cw <= 120, f'{what}: cart width a multiple of 4, at most the pitch and 120'
        assert pitch % 8 == 0 and pitch <= 120, f'{what}: pitch a multiple of 8, at most 120'
        assert chh + lift <= 96 and cw * chh <= 8640, f'{what}: cart height + lift at most 96, at most 8640 pixels'
        assert chh + lift <= foot <= 160, f'{what}: foot'
        cols = pitch // 8
        btw = -(-cw // 8)
        btw += (cols - btw) & 1
        big = btw * -(-(chh + lift) // 8)
        sw, sh = round(cw * lay['side_scale']), round(chh * lay['side_scale'])
        stw = -(-sw // 8)
        stw += (cols - stw) & 1
        small = stw * -(-sh // 8)
        assert big <= 135 and small <= 135, f'{what}: cart area over 135 tiles'
        assert 1 + 2 * big + 2 * small <= 480, f'{what}: cart tile areas do not fit VRAM'
    check('gba', CW, CH, lay['pitch'], lay['foot_y'])
    assert lx + lw <= CW and ly + lh <= CH and 4 * lay['label_colours'] <= 144
    assert 0 <= lay.get('side_dim', 0) <= 16
    for ext, sd, sc, scols in systems:
        w, h = sd['size']
        check(ext, w, h, sd.get('pitch', lay['pitch']), sd.get('foot_y', lay['foot_y']))
        x, y, lw, lh = sd['label']
        assert x + lw <= w and y + lh <= h, f'{ext} label'
    print(f'4bpp tiles: {len(tiles)}')
    assert len(tiles) + 54 <= 0x6000 // 32

    four = bytearray()
    for tl in tiles:
        four += pack4(tl)
    secs = [struct.pack('<112H', *[bgr555(c) for c in pal]), bytes(four),
            struct.pack('<600H', *map_back), struct.pack('<600H', *map_front), bytes(cart)]
    # after the five sections: per system {ext[4], labels[24], u32 cart, u32 palette,
    # s16 cart_w cart_h label_x label_y label_w label_h seat_w seat_h, pad} = 64 bytes,
    # then each system's palette (15 x u16, padded to 32) and cart bytes
    head = b'EFTH' + struct.pack('<HH', 1, len(PARAMS))
    head += t['name'].encode()[:23].ljust(24, b'\0') + t['labels'].encode()[:23].ljust(24, b'\0')
    off = 96 + 2 * len(PARAMS)
    off = (off + 3) & ~3
    table, body_data = b'', b''
    for s in secs:
        table += struct.pack('<II', off + len(body_data), len(s))
        body_data += s + b'\0' * ((4 - len(s) % 4) % 4)
    if systems:
        sys_off = off + len(body_data)
        extra = off + len(body_data) + 64 * len(systems)
        tbl, blobs = b'', b''
        for ext, sd, sc, scols in systems:
            palb = struct.pack('<16H', *([0] + [bgr555(c) for c in scols] + [0] * (15 - len(scols))))   # pixels are 97 + i
            pal_off = extra + len(blobs)
            blobs += palb
            cart_off = extra + len(blobs)
            blobs += bytes(sc) + b'\0' * ((4 - len(sc) % 4) % 4)
            back_off = 0
            if ext in alt_maps:
                back_off = extra + len(blobs)
                blobs += struct.pack('<600H', *alt_maps[ext])
            w, h = sd['size']
            sw, sh = sd.get('seated_size', [p['seat_w'], p['seat_h']])
            e = ext.encode()[:3].ljust(4, b'\0') + sd['labels'].encode()[:23].ljust(24, b'\0')
            e += struct.pack('<II8h', cart_off, pal_off, w, h, *sd['label'], min(sw, w), min(sh, h))
            e += struct.pack('<4hI', sd.get('pitch', 0), sd.get('foot_y', 0), sd.get('name_row', 0), 0, back_off)
            tbl += e.ljust(64, b'\0')
        body_data += tbl + blobs
        params[PARAMS.index('sys_off_lo')] = sys_off & 0xFFFF
        params[PARAMS.index('sys_off_hi')] = sys_off >> 16
        params = [v - 65536 if v > 32767 else v for v in params]
    data = head + table + struct.pack(f'<{len(PARAMS)}h', *params)
    data += b'\0' * (off - len(data)) + body_data
    open(out, 'wb').write(data)
    print(f'{out}: {len(data)} bytes')

    if preview:
        img = Image.new('RGB', (SW, SH))
        px = img.load()
        for ty in range(20):
            for tx in range(30):
                for m in (map_back, map_front):
                    e = m[ty * 30 + tx]
                    if not e and m is map_front:
                        continue
                    tl, b = tiles[e & 1023], e >> 12
                    for y in range(8):
                        for x in range(8):
                            v = tl[y * 8 + x]
                            if v or m is map_back:
                                px[tx * 8 + x, ty * 8 + y] = pal[b * 16 + v]
        for y in range(CH):
            for x in range(CW):
                v = cart[y * CW + x]
                if v:
                    px[(SW - CW) // 2 + x, lay['foot_y'] - CH - lift + y] = pal[v]
        img.resize((SW * 3, SH * 3), Image.NEAREST).save(preview)

main()
