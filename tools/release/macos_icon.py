"""Build a padded app icon from the existing logo using macOS's built-in tools."""
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image


def main():
    source, output, sips, iconutil = sys.argv[1:]
    with tempfile.TemporaryDirectory(prefix='sawgod-icon-') as scratch:
        root = Path(scratch)
        iconset = root / 'SawGod.iconset'
        iconset.mkdir()
        image = Image.open(source).convert('RGBA')
        image.thumbnail((896, 896), Image.Resampling.LANCZOS)
        canvas = Image.new('RGBA', (1024, 1024))
        canvas.alpha_composite(image, ((1024 - image.width) // 2, (1024 - image.height) // 2))
        master = root / 'master.png'
        canvas.save(master)
        for size in (16, 32, 128, 256, 512):
            for scale in (1, 2):
                dest = iconset / f'icon_{size}x{size}{"@2x" if scale == 2 else ""}.png'
                subprocess.run([sips, '-z', str(size * scale), str(size * scale), str(master),
                                '--out', str(dest)], check=True, stdout=subprocess.DEVNULL)
        subprocess.run([iconutil, '-c', 'icns', str(iconset), '-o', output], check=True)


if __name__ == '__main__':
    main()
