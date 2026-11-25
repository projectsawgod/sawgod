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
        # macOS icon grid: an 824 px white rounded square in the 1024 canvas, the mark inside it.
        sys.path.insert(0, str(Path(__file__).resolve().parent))
        from app_icons import white_tile
        canvas = white_tile(Image.open(source).convert('RGBA'), 1024, 824, 640)
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
