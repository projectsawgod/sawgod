#!/usr/bin/env python3
"""Bake the Enhanced title logo's kanji line (魔界塔士サ・ガ), its TM mark and the ENHANCED subtitle; no font ships.

Each glyph of Noto Serif CJK JP Bold is fitted to the box the original lettering occupies, then the line is
slanted like the original. The boxes and slant were fitted to the WSC title's own lettering (the WSC picture
is the source of truth; Enhanced only adds resolution). The output is white with alpha on the Enhanced logo canvas: native title pixels
x 24..200, y 0..96 at 8x (src/game/title_logo.h). The game paints the ROM's own strokes under it at run time.

The TM mark uses Noto Sans CJK JP Bold's Latin letters.

Usage: title_logo_kanji.py [--font PATH] [--tm-font PATH]   (default: the system's Noto Serif/Sans CJK Bold)
"""
import argparse
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from paths import ROOT

OUT = ROOT / 'assets/ui/title_kanji.png'
FONT = Path('/usr/share/fonts/opentype/noto/NotoSerifCJK-Bold.ttc')
TM_FONT = Path('/usr/share/fonts/opentype/noto/NotoSansCJK-Bold.ttc')  # the TM mark: a clean bold sans
SCALE = 8
CANVAS = (176 * SCALE, 96 * SCALE)  # the logo canvas: native x 24..200, y 0..96
TEXT = '魔界塔士サ・ガ'
# Each glyph's box in the original (x0, x1, y0, y1 on the canvas before the slant, native pixels)
BOXES = [(25.0, 43.25, 64.75, 80.25), (45.5, 62.5, 64.0, 79.75), (64.5, 81.75, 65.25, 80.25),
         (84.25, 100.75, 64.5, 79.0), (105.5, 123.5, 65.5, 79.25), (126.5, 130.25, 71.0, 74.0),
         (133.25, 152.25, 64.0, 80.0)]
SLANT = 0.24  # the original leans right: x shifts by SLANT per pixel above the line's bottom
BOTTOM = 80  # the line's bottom row, native
TM_BOX = (152, 77, 159, 80)  # the original TM mark, native canvas pixels (exclusive right/bottom), upright
SUBTITLE = 'ENHANCED'  # movie-poster style: upright serif capitals, widely spaced, centered under the kanji line
SUBTITLE_CENTER = (25 + 152.25) / 2  # the kanji line's middle at its bottom, native
SUBTITLE_TOP, SUBTITLE_HEIGHT, SUBTITLE_WIDTH = 82.5, 4.5, 84  # native pixels; the canvas fades from row 88


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--font', type=Path, default=FONT)
    parser.add_argument('--tm-font', type=Path, default=TM_FONT)
    args = parser.parse_args()
    font = ImageFont.truetype(str(args.font), 200, index=0)  # index 0: the JP face
    alpha = Image.new('L', CANVAS, 0)
    for char, (x0, x1, y0, y1) in zip(TEXT, BOXES):
        glyph = Image.new('L', (260, 260), 0)
        ImageDraw.Draw(glyph).text((30, 0), char, font=font, fill=255)
        glyph = glyph.crop(glyph.getbbox())
        size = (round((x1 - x0) * SCALE), round((y1 - y0) * SCALE))
        alpha.paste(glyph.resize(size, Image.LANCZOS), (round(x0 * SCALE), round(y0 * SCALE)),
                    glyph.resize(size, Image.LANCZOS))
    alpha = alpha.transform(alpha.size, Image.AFFINE, (1, SLANT, -SLANT * BOTTOM * SCALE, 0, 1, 0),
                            resample=Image.BICUBIC)
    # The TM mark in sans Latin letters, fitted to the original's box
    tm = Image.new('L', (400, 260), 0)
    ImageDraw.Draw(tm).text((20, 0), 'TM', font=ImageFont.truetype(str(args.tm_font), 200, index=0), fill=255)
    tm = tm.crop(tm.getbbox())
    box = [v * SCALE for v in TM_BOX]
    tm = tm.resize((box[2] - box[0], box[3] - box[1]), Image.LANCZOS)
    alpha.paste(tm, (box[0], box[1]), tm)
    # The subtitle, drawn large and scaled down, each letter set on its own so the spacing is even.
    big = ImageFont.truetype(str(args.font), 400, index=0)
    letters = []
    for char in SUBTITLE:
        glyph = Image.new('L', (560, 560), 0)
        ImageDraw.Draw(glyph).text((40, 0), char, font=big, fill=255)
        letters.append(glyph.crop(glyph.getbbox()))
    height = max(g.height for g in letters)
    shrink = SUBTITLE_HEIGHT * SCALE / height
    widths = [g.width * shrink for g in letters]
    gap = (SUBTITLE_WIDTH * SCALE - sum(widths)) / (len(letters) - 1)
    x = (SUBTITLE_CENTER - SUBTITLE_WIDTH / 2) * SCALE
    for glyph, width in zip(letters, widths):
        small = glyph.resize((round(width), round(glyph.height * shrink)), Image.LANCZOS)
        alpha.paste(small, (round(x), round(SUBTITLE_TOP * SCALE)), small)
        x += width + gap
    out = Image.new('RGBA', CANVAS, (255, 255, 255, 0))
    out.putalpha(alpha)
    out.save(OUT, optimize=True)
    print(f'{OUT}: {CANVAS[0]}x{CANVAS[1]}')


if __name__ == '__main__':
    main()
