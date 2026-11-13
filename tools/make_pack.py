#!/usr/bin/env python3
"""Label pack for EverFront from a folder of label scans (any system).

usage: make_pack.py <labels dir> <out labels.bin> [--size WxH] [--colours N]
                    [--dat gba.dat] [--review review.png]

  --size, --colours  the theme's label well; default 88x45 in 36 colours (the
                     Slot theme's GBA labels). Shelf's GBA labels want
                     --size 80x41; other systems' sizes are in the README.
  --dat              optional: a No-Intro / libretro GBA .dat with serials, so
                     a label also matches a rom by the game code in its header
                     (handy when rom and scan names differ)
  --review           optional: a contact sheet of the converted labels
  --cache DIR        optional: keep each converted label in DIR, so a later
                     run only converts new or changed scans
  --budget SECONDS   optional (with --cache): stop converting after this long
                     and exit with code 3 without writing the pack; run again
                     to continue (for slow machines and big folders)
  --also DIR         optional, repeatable: more scans, used where the main
                     folder has none (e.g. Game Boy labels in the GBC pack)
  --alias FILE       optional: lines "rom name = label name" (no extensions),
                     for roms whose names don't match a scan (hacks, renames)

Needs Python 3 and Pillow (pip install pillow).

Scans are named like the game (No-Intro style, any size, ~1.94:1). Each
becomes an 88x45 label (Slot's label well at 1/3) in 36 colours (undithered).

labels.bin (little endian):
  "EFLB" u16 version(3) u16 label_w u16 label_h u16 ncolours u32 count
  count x { u32 key, u32 offset }  sorted by key
  at offset: 36 x u16 BGR555, then label_w*label_h bytes (0-35, 255 = no
  label here: the cart's plastic shows through the scan's rounded corners)
Keys (lookup in this order on the GBA):
  full name   (crc32(lowercase name without extension) & 0x3FFFFFFF) | 0x80000000
  game code   the header's 4 ASCII bytes as a little-endian u32
  title only  (crc32(lowercase name up to the first " (") & 0x3FFFFFFF) | 0xC0000000
"""
import os, re, struct, sys, zlib
import warnings; warnings.filterwarnings('ignore')
from PIL import Image, ImageEnhance, ImageFilter

LW, LH = 88, 45
NCOL = 36

def bgr555(c):
    r, g, b = c[:3]
    return (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)

def key_name(s):
    return (zlib.crc32(s.lower().encode()) & 0x3FFFFFFF) | 0x80000000

def key_title(s):
    return (zlib.crc32(s.split(' (')[0].split(' [')[0].lower().encode()) & 0x3FFFFFFF) | 0xC0000000

def key_code(code):
    return struct.unpack('<I', code.encode())[0]

def load_dat(path):
    by_name, by_title = {}, {}
    for block in re.split(r'\ngame \(', open(path, encoding='utf-8', errors='replace').read()):
        n = re.search(r'name "([^"]+)"', block)
        sr = re.search(r'serial "([^"]*)"', block)
        if not n or not sr or len(sr.group(1)) != 4:
            continue
        by_name[n.group(1).lower()] = sr.group(1)
        t = n.group(1).split(' (')[0].lower()
        by_title.setdefault(t, []).append((n.group(1), sr.group(1)))
    return by_name, by_title

def code_for(stem, by_name, by_title):
    c = by_name.get(stem.lower())
    if c:
        return c
    # same title, prefer a matching region
    cands = by_title.get(stem.split(' (')[0].lower(), [])
    regions = re.findall(r'\(([^)]*)\)', stem)
    reg = regions[0] if regions else ''
    for n, s in cands:
        if reg and reg.split(',')[0] in n:
            return s
    return cands[0][1] if len(cands) == 1 else None

def label(path):
    """Straight Lanczos to 88x45 and a light unsharp mask (the scan's own
    detail, nothing smeared twice), then 36 colours by median cut refined
    with k-means, no dithering: dither noise is what made small text hard to
    read at this size."""
    im = Image.open(path).convert('RGBA')
    alpha = list(im.resize((LW, LH), Image.LANCZOS).getchannel('A').getdata())
    rgb = Image.new('RGB', im.size, (0, 0, 0))
    rgb.paste(im.convert('RGB'), (0, 0), im.getchannel('A'))
    rgb = rgb.resize((LW, LH), Image.LANCZOS).filter(ImageFilter.UnsharpMask(1, 60, 2))
    q = rgb.quantize(NCOL, method=Image.MEDIANCUT, kmeans=4, dither=Image.NONE)
    pal = q.getpalette()[:NCOL * 3]
    pal = pal[:len(pal) // 3 * 3]
    cols = [tuple(pal[i * 3:i * 3 + 3]) for i in range(NCOL)]
    cols = [c if len(c) == 3 else (0, 0, 0) for c in cols]
    cols += [(0, 0, 0)] * (NCOL - len(cols))
    idx = [v if a > 128 else 255 for v, a in zip(q.getdata(), alpha)]
    return cols, idx

def opt(args, name):
    if name in args:
        k = args.index(name)
        v = args[k + 1]
        del args[k:k + 2]
        return v
    return None

def main():
    global LW, LH, NCOL
    args = sys.argv[1:]
    datp, review = opt(args, '--dat'), opt(args, '--review')
    cache, budget = opt(args, '--cache'), opt(args, '--budget')
    alias_file = opt(args, '--alias')
    also = []
    while '--also' in args:
        also.append(opt(args, '--also'))
    import time, hashlib
    t0 = time.time()
    if '--size' in args:
        k = args.index('--size')
        LW, LH = map(int, args[k + 1].split('x'))
        del args[k:k + 2]
    if '--colours' in args:
        k = args.index('--colours')
        NCOL = int(args[k + 1])
        del args[k:k + 2]
    if len(args) == 3 and args[1].lower().endswith('.dat'):   # old form: <labels> <dat> <out> [review]
        datp, args = args[1], [args[0], args[2]] + args[3:]
    if len(args) == 3:
        review = args[2]
    if len(args) < 2:
        sys.exit(__doc__)
    src, out = args[0], args[1]
    by_name, by_title = load_dat(datp) if datp else ({}, {})
    files, seen_stems = [], set()
    for d in [src] + also:                       # the main folder first: its scans win
        for f in sorted(os.listdir(d)):
            if f.lower().endswith('.png') and f.lower() not in seen_stems:
                seen_stems.add(f.lower())
                files.append((d, f))
    keys = {}            # key -> blob index
    blobs = []
    previews = []
    weak = {}            # title keys can collide: first one wins
    coded = 0
    if cache:
        cdir = os.path.join(cache, f'{LW}x{LH}x{NCOL}')
        os.makedirs(cdir, exist_ok=True)
    left = 0
    for d, f in files:
        stem = os.path.splitext(f)[0]
        path = os.path.join(d, f)
        blob = None
        if cache:
            st = os.stat(path)
            cf = os.path.join(cdir, hashlib.sha1(f'{f}|{st.st_size}|{int(st.st_mtime)}'.encode()).hexdigest() + '.lbl')
            if os.path.exists(cf):
                blob = open(cf, 'rb').read()
            elif budget and time.time() - t0 > float(budget):
                left += 1
                continue
        if blob is None:
            try:
                cols, idx = label(path)
            except Exception as e:
                print('skip', f, e)
                continue
            blob = struct.pack(f'<{NCOL}H', *[bgr555(c) for c in cols]) + bytes(idx)
            if cache:
                open(cf, 'wb').write(blob)
        else:
            pal = struct.unpack_from(f'<{NCOL}H', blob)
            cols = [((v & 31) << 3, ((v >> 5) & 31) << 3, ((v >> 10) & 31) << 3) for v in pal]
            idx = list(blob[2 * NCOL:])
        bi = len(blobs)
        blobs.append(blob)
        keys[key_name(stem)] = bi
        code = code_for(stem, by_name, by_title)
        if code:
            coded += 1
            keys.setdefault(key_code(code), bi)
        weak.setdefault(key_title(stem), bi)
        if review:
            p = Image.new('RGB', (LW, LH), (40, 40, 40))
            p.putdata([cols[v] if v != 255 else (40, 40, 40) for v in idx])
            previews.append(p)
    if left:
        print(f'{len(blobs)} converted, {left} still to do: run again')
        sys.exit(3)
    for k, bi in weak.items():
        keys.setdefault(k, bi)
    if alias_file:
        for line in open(alias_file, encoding='utf-8'):
            if '=' not in line or line.lstrip().startswith('#'):
                continue
            rom, lab = (x.strip() for x in line.split('=', 1))
            bi = keys.get(key_name(lab))
            if bi is None:
                print('alias: no scan named', lab)
                continue
            keys[key_name(rom)] = bi
            keys.setdefault(key_title(rom), bi)
    order = sorted(keys)
    head = 16 + 8 * len(order)
    blob_size = 2 * NCOL + LW * LH
    data = b'EFLB' + struct.pack('<HHHHI', 3, LW, LH, NCOL, len(order))
    data += b''.join(struct.pack('<II', k, head + keys[k] * blob_size) for k in order)
    data += b''.join(blobs)
    open(out, 'wb').write(data)
    print(f'{len(blobs)} labels ({coded} with a game code), {len(order)} keys, {len(data)} bytes')
    if review and previews:
        n = 8
        sheet = Image.new('RGB', (n * (LW + 2), ((len(previews) + n - 1) // n) * (LH + 2)), (20, 20, 20))
        for i, p in enumerate(previews):
            sheet.paste(p, ((i % n) * (LW + 2), (i // n) * (LH + 2)))
        sheet.resize((sheet.width * 2, sheet.height * 2), Image.NEAREST).save(review)

main()
