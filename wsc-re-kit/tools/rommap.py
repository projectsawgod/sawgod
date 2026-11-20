#!/usr/bin/env python3
"""Print per-bank entropy and code/data coverage as a Markdown table."""
import argparse
from collections import Counter
import math
from pathlib import Path
from cartridge import BANK, load, pairs
from cdl import read


def statistics(block):
    counts = Counter(block)
    return (-sum(n / len(block) * math.log2(n / len(block)) for n in counts.values()),
            100 * counts[0xFF] / len(block))


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("--cdl", type=Path)
    ap.add_argument("--code", type=Path)
    a = ap.parse_args()
    cart = load(a.rom)
    flags, code = read(a.cdl, cart) if a.cdl else {}, pairs(a.code)
    ran, data, found = Counter(), Counter(), Counter()
    for o, f in flags.items():
        ran[o // BANK] += bool(f & 1)
        data[o // BANK] += bool(f & 2)
    for o, n in code.items():
        found[o // BANK] += n
    print("| Bank | File offset | Entropy | FF % | Opcodes run | Code bytes | Data bytes read |")
    print("|---|---|---|---|---|---|---|")
    for b in range(cart.size // BANK):
        entropy, ff = statistics(cart.data[b * BANK:(b + 1) * BANK])
        print(f"| {b:02X} | {b * BANK:06X} | {entropy:.2f} | {ff:.1f} | {ran[b]} | {found[b]} | {data[b]} |")


if __name__ == "__main__":
    main()
