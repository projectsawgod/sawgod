"""Fetch the pinned, redistributable soundfont for release CI. No ROM data."""
import hashlib
from pathlib import Path
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
SHA256 = '9575028c7a1f589f5770fccc8cff2734566af40cd26ed836944e9a5152688cfe'
URL = 'https://raw.githubusercontent.com/mrbumpy409/GeneralUser-GS/684543d/GeneralUser-GS.sf2'


def main():
    dest = ROOT / 'ref/soundfonts/GeneralUser-GS.sf2'
    dest.parent.mkdir(parents=True, exist_ok=True)
    data = dest.read_bytes() if dest.exists() else urllib.request.urlopen(URL, timeout=120).read()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError('GeneralUser GS 2.0.3 checksum mismatch')
    dest.write_bytes(data)


if __name__ == '__main__':
    main()
