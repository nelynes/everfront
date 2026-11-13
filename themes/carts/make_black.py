#!/usr/bin/env python3
"""Black carts for the Slot theme from Nelynes's 50%-grey shells (Game Gear,
Master System): the shading kept, the plastic taken down to black.

    python3 make_black.py   -> art/gg_black.png, art/sms_black.png
"""
import os
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
GAMMA, RELIEF = 2.2, 2.0              # relief doubled around 50% grey, then darkened: 128 -> 56, so the moulding reads on black
for name in ('gg', 'sms'):
    im = Image.open(os.path.join(here, 'art', name + '_neutral.png')).convert('RGBA')
    px = [tuple(round(255 * (min(255, max(0, 128 + (v - 128) * RELIEF)) / 255) ** GAMMA) + (2 if c == 2 else 0) for c, v in enumerate((r, g, b))) + (a,)
          for r, g, b, a in im.get_flattened_data()]
    out = Image.new('RGBA', im.size)
    out.putdata(px)
    out.save(os.path.join(here, 'art', name + '_black.png'))
