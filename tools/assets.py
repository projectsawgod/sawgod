#!/usr/bin/env python3
"""assets.py - extract ROM assets described by assets/manifest/*.yaml, and prove the round trip.

  assets.py extract [--out build/assets]   write each asset as an editable file (PNG, JSON, .bin)
  assets.py check                          extract -> rebuild every asset in memory; must equal the ROM bytes
  assets.py archive [--out build/sawgod.gsa]
                                           the base asset archive the port loads: every manifest asset, binary
  assets.py pack DIR --out MOD.gsa         an override pack from edited files in DIR (same names and suffixes
                                           as `extract` writes; only the files present); layered over the base
  assets.py import-ips PATCH.ips --out MOD.gsa
                                           an override pack from an IPS patch for the supported ROM (e.g. the
                                           user's Tower Reversed English patch, which sawgod --english loads):
                                           one entry per changed run of bytes, named ips/<offset>

Archive format (little-endian): "GSAWPAK1", the SHA-1 (20 bytes) of the ROM the assets belong to, u32 kind
(0 base, 1 override pack), u32 entry count, then per entry: u16 name length, name (UTF-8), u32 ROM offset,
u32 size, data. The port checks the ROM's SHA-1 against it; entries are laid over the ROM image at their
offsets for both native and interpreted execution. This is the legacy byte-archive/import path;
ordinary resource mods use .sgz archives or folders (tools/sgz.py, docs/MODS.md).

Manifest entries: name, rom (offset), size, format (tiles_4bpp | strings | records | raw) plus format
options (cols; record_size + fields); or format `table` with rows + parts (game data as one human-readable CSV
per table, its parts possibly in several banks; tools/sawgod/tables.py). Entries must not overlap. Nothing
extracted is ever committed.
"""
import argparse
import hashlib
import json
import struct
import sys
from pathlib import Path

import yaml

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sawgod.formats import FORMATS, dumps  # noqa: E402
from sawgod.rom import load_baseline  # noqa: E402
from sawgod import tables  # noqa: E402
from paths import SAWGOD_ROM, ROOT  # noqa: E402

MANIFEST = ROOT / "assets" / "manifest"
# CSV table formats: (regions, extract(rom, entry) -> csv bytes, build(csv, entry, rom) -> [(rom offset, bytes)])
TABLES = {
    "table": (tables.regions, tables.extract, lambda c, e, rom: tables.build(c, e)),
    "pointer_records": (tables.pointer_regions, tables.pointer_extract, tables.pointer_build),
    "formations": (tables.pointer_regions, tables.formation_extract, tables.formation_build),
}


def load_manifest():
    entries = []
    for p in sorted(MANIFEST.glob("*.yaml")):
        doc = yaml.safe_load(p.read_text(encoding="utf-8")) or {}
        for e in doc.get("assets", []):
            e["_file"] = p.name
            if e["format"] in TABLES:  # for sorting and the coverage count (its own rom/size keys stay as written)
                e["_sort"] = TABLES[e["format"]][0](e)[0][0]
                e["_bytes"] = sum(n for _, n in TABLES[e["format"]][0](e))
            else:
                e["_sort"], e["_bytes"] = e["rom"], e["size"]
            entries.append(e)
    entries.sort(key=lambda e: e["_sort"])
    spans = sorted((r, r + n, e["name"]) for e in entries for r, n in region_list(e))
    for a, b in zip(spans, spans[1:]):
        if a[1] > b[0]:
            raise SystemExit(f"manifest overlap: {a[2]} and {b[2]}")
    return entries


def region_list(e):
    """(rom, size) of each ROM region an entry covers (a table: one per part)."""
    return TABLES[e["format"]][0](e) if e["format"] in TABLES else [(e["rom"], e["size"])]


def archive_items(e, regions_data):
    """Archive entries for an asset: (name, rom, data); a table's parts become NAME#0, NAME#1..."""
    if e["format"] not in TABLES:
        return [(e["name"], e["rom"], regions_data[0][1])]
    return [(f"{e['name']}#{i}", r, d) for i, (r, d) in enumerate(regions_data)]


def write_archive(path, rom_sha1, kind, items):
    out = bytearray(b"GSAWPAK1" + rom_sha1 + struct.pack("<II", kind, len(items)))
    for name, offset, data in items:
        n = name.encode("utf-8")
        out += struct.pack("<H", len(n)) + n + struct.pack("<II", offset, len(data)) + data
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(bytes(out))


def pack(src, out, rom, entries):
    items = []
    for e in entries:
        matches = [p for p in src.glob(e["name"] + ".*") if p.is_file()]
        if not matches:
            continue
        f = matches[0]
        raw = f.read_bytes()
        if e["format"] in TABLES:
            try:
                parts = TABLES[e["format"]][2](raw, e, rom)
            except (ValueError, KeyError) as err:
                raise SystemExit(f"{f}: {err}")
            changed = any(d != rom[r:r + len(d)] for r, d in parts)
            print(f"  {e['name']}: {'changed' if changed else 'unchanged'}")
            items += archive_items(e, parts)
            continue
        _, build = FORMATS[e["format"]]
        payload = raw if f.suffix in (".png", ".bin") else json.loads(raw.decode("utf-8"))
        data = build(payload, e)
        if len(data) != e["size"]:
            raise SystemExit(f"{f}: rebuilds to {len(data)} bytes, the asset is {e['size']} (sizes must match)")
        changed = data != rom[e["rom"]:e["rom"] + e["size"]]
        print(f"  {e['name']}: {'changed' if changed else 'unchanged'}")
        items.append((e["name"], e["rom"], data))
    write_archive(out, hashlib.sha1(rom).digest(), 1, items)
    print(f"pack: {len(items)} asset(s) -> {out}")


def read_ips(path, rom):
    """The ROM with the IPS patch at `path` applied (records, RLE records and the truncate extension)."""
    d = Path(path).read_bytes()
    if d[:5] != b"PATCH":
        raise SystemExit(f"{path}: not an IPS patch")
    out, i = bytearray(rom), 5
    while True:
        if i + 3 > len(d):
            raise SystemExit(f"{path}: truncated (no EOF marker)")
        if d[i:i + 3] == b"EOF":
            break
        off, size = int.from_bytes(d[i:i + 3], "big"), int.from_bytes(d[i + 3:i + 5], "big")
        i += 5
        if size == 0:
            size, data = int.from_bytes(d[i:i + 2], "big"), bytes([d[i + 2]]) * int.from_bytes(d[i:i + 2], "big")
            i += 3
        else:
            data = d[i:i + size]
            i += size
        if off + size > len(out):
            raise SystemExit(f"{path}: a record at {off:#x} runs past the end of the ROM")
        out[off:off + size] = data
    if len(d) >= i + 6 and len(out) != int.from_bytes(d[i + 3:i + 6], "big"):
        raise SystemExit(f"{path}: the patch resizes the ROM, which SawGod can't load")
    return bytes(out)


def import_ips(patch, out, rom):
    new = read_ips(patch, rom)
    items, k = [], 0
    while k < len(rom):  # the changed runs; gaps under 16 bytes are merged into one entry
        if new[k] == rom[k]:
            k += 1
            continue
        start, end = k, k + 1
        while end < len(rom) and any(new[j] != rom[j] for j in range(end, min(end + 16, len(rom)))):
            end += 1
        while new[end - 1] == rom[end - 1]:
            end -= 1
        items.append((f"ips/{start:06X}", start, new[start:end]))
        k = end
    write_archive(out, hashlib.sha1(rom).digest(), 1, items)
    print(f"import-ips: {sum(len(d) for _, _, d in items)} bytes in {len(items)} entries -> {out} "
          f"(patched ROM SHA-1 {hashlib.sha1(new).hexdigest()})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("cmd", choices=("extract", "check", "archive", "pack", "import-ips"))
    ap.add_argument("src", nargs="?", type=Path,
                    help="pack: directory of edited asset files; import-ips: the IPS patch")
    ap.add_argument("--out", type=Path)
    a = ap.parse_args()
    rom = load_baseline(SAWGOD_ROM)
    entries = load_manifest()
    if a.cmd == "archive":
        out = a.out or ROOT / "build" / "sawgod.gsa"
        write_archive(out, hashlib.sha1(rom).digest(), 0,
                      [it for e in entries
                       for it in archive_items(e, [(r, rom[r:r + n]) for r, n in region_list(e)])])
        print(f"archive: {len(entries)} assets -> {out}")
        return 0
    if a.cmd == "pack":
        if not a.src or not a.out:
            raise SystemExit("pack needs DIR and --out")
        pack(a.src, a.out, rom, entries)
        return 0
    if a.cmd == "import-ips":
        if not a.src or not a.out:
            raise SystemExit("import-ips needs PATCH and --out")
        import_ips(a.src, a.out, rom)
        return 0
    a.out = a.out or ROOT / "build" / "assets"
    bad, covered = 0, 0
    for e in entries:
        if e["format"] in TABLES:
            payload = TABLES[e["format"]][1](rom, e)
            if a.cmd == "extract":
                out = a.out / (e["name"] + ".csv")
                out.parent.mkdir(parents=True, exist_ok=True)
                out.write_bytes(payload)
            else:
                for r, d in TABLES[e["format"]][2](payload, e, rom):
                    if d != rom[r:r + len(d)]:
                        bad += 1
                        print(f"MISMATCH {e['name']} ({e['format']}): part at {r:#x}")
            covered += e["_bytes"]
            continue
        data = rom[e["rom"]:e["rom"] + e["size"]]
        ext, build = FORMATS[e["format"]]
        suffix, payload = ext(data, e)
        if a.cmd == "extract":
            out = a.out / (e["name"] + suffix)
            out.parent.mkdir(parents=True, exist_ok=True)
            out.write_bytes(payload if isinstance(payload, bytes) else dumps(payload))
        else:
            back = build(payload if isinstance(payload, bytes) else json.loads(dumps(payload)), e)
            if back != data:
                bad += 1
                k = next((i for i in range(min(len(back), len(data))) if back[i] != data[i]), None)
                print(f"MISMATCH {e['name']} ({e['format']}): first difference at +{k}, sizes {len(back)}/{len(data)}")
        covered += e["size"]
    print(f"assets: {len(entries)} entries, {covered} bytes ({100 * covered / len(rom):.1f}% of the ROM), "
          f"{'extracted to ' + str(a.out) if a.cmd == 'extract' else f'{bad} round-trip failure(s)'}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
