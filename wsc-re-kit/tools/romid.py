#!/usr/bin/env python3
"""Print cartridge geometry, footer fields, checksums and content hashes as JSON."""
import argparse
import hashlib
import json
from pathlib import Path
import zlib
from cartridge import Cartridge


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("--strict", action="store_true", help="require recognized geometry and a valid checksum")
    a = ap.parse_args()
    c = Cartridge(a.rom.read_bytes())
    print(json.dumps({"size": c.size, "declared_size": c.declared_size, "mapper": c.mapper,
                      "save_type": c.save_type, "save_size": c.save_size, "color": c.color,
                      "flags": c.data[-4], "checksum": c.checksum,
                      "computed_checksum": c.computed_checksum,
                      "sha1": hashlib.sha1(c.data).hexdigest(), "crc32": f"{zlib.crc32(c.data):08x}"}, indent=2))
    if a.strict:
        c.validate()
        return int(c.checksum != c.computed_checksum)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
