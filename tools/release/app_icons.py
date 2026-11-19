"""Regenerate the app icons from the SG mark.

  python tools/release/app_icons.py --master SOURCE.png   # once: SOURCE (mark on white) -> assets/ui/sawgod_sg_mark.png
  python tools/release/app_icons.py                       # icons from assets/ui/sawgod_sg_mark.png

Writes the Windows .ico, the runtime window icon and the two Android launcher images. The macOS .icns is built
from the same master at build time (cmake/MacBundle.cmake, tools/release/macos_icon.py).
"""
import argparse
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
MASTER = ROOT / 'assets/ui/sawgod_sg_mark.png'
WINDOW_ICON = ROOT / 'assets/ui/app_icon_256.png'
WINDOWS_ICO = ROOT / 'assets/ui/sawgod.ico'
ANDROID_RES = ROOT / 'android/app/src/main/res'


def master_from_white(source):
    """The mark on white -> the mark on transparency, cropped to its ink (white removed as a color)."""
    rgb = np.asarray(Image.open(source).convert('RGB')).astype(np.float64)
    alpha = (255.0 - rgb).max(axis=2) / 255.0
    safe = np.where(alpha > 0, alpha, 1.0)[..., None]
    color = np.clip(255.0 - (255.0 - rgb) / safe, 0, 255)
    rgba = np.dstack([color, alpha * 255.0]).round().astype(np.uint8)
    image = Image.fromarray(rgba, 'RGBA')
    image = image.crop(image.getchannel('A').point(lambda a: 255 if a > 8 else 0).getbbox())
    image.save(MASTER, optimize=True)


def fitted(mark, size, box, background=None):
    """The mark scaled to fit a box x box square, centered on a size x size canvas."""
    image = mark.copy()
    image.thumbnail((box, box), Image.Resampling.LANCZOS)
    canvas = Image.new('RGBA', (size, size), background or (0, 0, 0, 0))
    canvas.alpha_composite(image, ((size - image.width) // 2, (size - image.height) // 2))
    return canvas


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--master', type=Path, help='rebuild the master from this mark-on-white image first')
    args = parser.parse_args()
    if args.master:
        master_from_white(args.master)
    mark = Image.open(MASTER).convert('RGBA')
    fitted(mark, 256, 240).save(WINDOW_ICON, optimize=True)
    fitted(mark, 256, 240).save(WINDOWS_ICO, sizes=[(s, s) for s in (16, 24, 32, 48, 64, 128, 256)])
    # Adaptive icon: 108 dp canvas, launchers may mask to a 66 dp circle; the mark's corners stay inside it.
    fitted(mark, 432, 220).save(ANDROID_RES / 'drawable/icon_foreground.png', optimize=True)
    white = (255, 255, 255, 255)
    fitted(mark, 192, 150, white).save(ANDROID_RES / 'mipmap-xxxhdpi/ic_launcher.png', optimize=True)


if __name__ == '__main__':
    main()
