#!/usr/bin/env python3
"""Bake fixed touch labels/icons and the runtime atlas; no font is shipped.

Requires Pillow and DejaVu Sans Bold (override with --font). Kenney sources in
assets/ui/touch are copied unchanged from Mobile Controls 1.0, Style C / Icons.
"""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / 'assets/ui/touch'


def chevrons(d, n):
    w = n // 7
    for x in (n * .18, n * .48):
        pts = [(x, n * .2), (x + n * .3, n * .5), (x, n * .8)]
        d.line(pts, fill='white', width=w, joint='curve')
        for px, py in (pts[0], pts[2]):
            d.ellipse((px - w / 2, py - w / 2, px + w / 2, py + w / 2), fill='white')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--font', default='/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf')
    a = p.parse_args()
    im = Image.new('RGBA', (264, 264))
    chevrons(ImageDraw.Draw(im), 264)
    im.resize((66, 66), Image.Resampling.LANCZOS).rotate(90).save(OUT / 'mode.png')
    for name, text, size, box in [('start', 'START', 38, (240, 120)),
                                  ('y', 'Y', 44, (80, 80)), ('x', 'X', 44, (80, 80))]:
        im = Image.new('RGBA', box)
        ImageDraw.Draw(im).text((box[0] / 2, box[1] / 2), text, fill='white',
                               font=ImageFont.truetype(a.font, size), anchor='mm')
        im.save(OUT / f'{name}.png')
    names = ['dpad_separate', 'button_circle', 'button_circle_wide', 'icon_menu',
             'icon_button_a', 'icon_button_b', 'mode', 'start', 'y', 'x']
    # Stay within a 2048-pixel texture edge for older mobile renderers.
    atlas = Image.new('RGBA', (2048, 1536))
    for i, name in enumerate(names):
        im = Image.open(OUT / f'{name}.png').convert('RGBA')
        # Destination aspect restores wide pills/labels. Source icons use their native aspect.
        if name in ('button_circle_wide', 'start'):
            im = im.resize((512, 512), Image.Resampling.LANCZOS)
        else:
            im.thumbnail((512, 512), Image.Resampling.LANCZOS)
            # Upscale small icons as well; the runtime uses linear filtering.
            scale = 512 / max(im.size)
            im = im.resize((round(im.width * scale), round(im.height * scale)), Image.Resampling.LANCZOS)
        atlas.alpha_composite(im, ((i % 4) * 512 + (512 - im.width) // 2,
                                   (i // 4) * 512 + (512 - im.height) // 2))
    atlas.save(OUT / 'atlas.png')


if __name__ == '__main__':
    main()
