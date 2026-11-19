#!/usr/bin/env python3
"""leakscan.py - fail if a file in the repo contains a run of bytes copied from the ROM.

Source derived from the ROM (asm, C) is fine; binary ROM data is not. Any run of at least
--min bytes (default 32) that also occurs in the baseline ROM is reported, unless the run is
low-entropy filler (fewer than 4 distinct byte values).

Usage: leakscan.py [--staged [--overlay] | --all | FILE ...] [--min N]
  --staged  staged bytes in the public index (the pre-commit hook uses this)
  --overlay with --staged: the sawgod-dev overlay's index (its pre-commit hook)
  --all     every tracked file in both repositories (default)
Without the ROM (public CI, contributors who haven't set one up) it warns and exits 0.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import repo_files as repo
from paths import SAWGOD_ROM, ROOT  # noqa: E402

K = 16  # indexed gram length; any run >= 2*K contains a K-aligned ROM gram


def low_entropy(b):
    return len(set(b)) < 4


def build_index(rom):
    idx = {}
    for off in range(0, len(rom) - K + 1, K):
        g = rom[off:off + K]
        if not low_entropy(g):
            idx.setdefault(g, []).append(off)
    return idx


def scan(data, rom, idx, minlen):
    """Yield (file_offset, rom_offset, length) for each maximal match >= minlen."""
    i, n = 0, len(data)
    while i <= n - K:
        offs = idx.get(data[i:i + K])
        if not offs:
            i += 1
            continue
        best = None
        for ro in offs:
            s, r = i, ro  # extend backwards then forwards
            while s > 0 and r > 0 and data[s - 1] == rom[r - 1]:
                s -= 1
                r -= 1
            e, re_ = i + K, ro + K
            while e < n and re_ < len(rom) and data[e] == rom[re_]:
                e += 1
                re_ += 1
            if best is None or e - s > best[2]:
                best = (s, r, e - s)
        if best[2] >= minlen and not low_entropy(data[best[0]:best[0] + best[2]]):
            yield best
            i = best[0] + best[2]
        else:
            i += 1


def git_files(staged, overlay=False):
    return [ROOT / p for p in sorted(repo.staged(ROOT, overlay=overlay) if staged else repo.both(ROOT))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("files", nargs="*", type=Path)
    ap.add_argument("--staged", action="store_true")
    ap.add_argument("--all", action="store_true")
    ap.add_argument("--overlay", action="store_true")
    ap.add_argument("--min", type=int, default=2 * K)
    a = ap.parse_args()
    if a.min < 2 * K:
        ap.error(f"--min must be at least {2 * K}")
    if not SAWGOD_ROM.is_file():
        print(f"leakscan: no ROM at {SAWGOD_ROM}; skipped", file=sys.stderr)
        return 0
    rom = SAWGOD_ROM.read_bytes()
    idx = build_index(rom)
    files = a.files or git_files(a.staged, a.overlay)
    bad = 0
    for f in files:
        if (not a.staged or a.files) and not f.is_file():
            continue
        data = (repo.git(ROOT, 'show', ':' + f.relative_to(ROOT).as_posix(), overlay=a.overlay, index_env=True)
                if a.staged and not a.files else f.read_bytes())
        for fo, ro, ln in scan(data, rom, idx, a.min):
            bad += 1
            print(f"LEAK {f.relative_to(ROOT) if f.is_relative_to(ROOT) else f}: "
                  f"{ln} bytes at file 0x{fo:X} match ROM 0x{ro:06X}")
    print(f"leakscan: {len(files)} files, {bad} leak(s)", file=sys.stderr)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
