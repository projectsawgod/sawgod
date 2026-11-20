#!/usr/bin/env python3
"""cyclecost.py - compare per-instruction cycle costs of two CPU traces that needn't line up exactly.

  cyclecost.py ORACLE OURS [--top N]

Groups instructions by address (cs:ip) and compares the mean cycle delta (to the next traced instruction)
in each trace; lists the addresses where the difference, times the execution count, is largest. Deltas
above 60 cycles (interrupts, DMA) are ignored.
"""
import argparse
from collections import defaultdict
from pathlib import Path
from cartridge import Mapping, load, mapping_args


def costs(path):
    tot, cnt = defaultdict(int), defaultdict(int)
    prev = None
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("frame"):
                prev = None
                continue
            p = line.split(None, 2)
            if not p:
                continue
            c, pc = int(p[0]), p[1]
            if prev and 0 <= c - prev[0] <= 60:
                tot[prev[1]] += c - prev[0]
                cnt[prev[1]] += 1
            prev = (c, pc)
    return tot, cnt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("oracle")
    ap.add_argument("ours")
    ap.add_argument("--top", type=int, default=40)
    ap.add_argument("--rom", type=Path, help="optional opcode-byte annotation")
    mapping_args(ap)
    a = ap.parse_args()
    cart = load(a.rom) if a.rom else None
    mapping = Mapping(cart, a.linear_page, a.bank0, a.bank1) if cart else None

    def code(pc):
        if mapping is None:
            return "?"
        cs, ip = (int(x, 16) for x in pc.split(":"))
        off = mapping.rom((cs << 4) + ip)
        return cart.data[off:off + 3].hex() if off is not None else "?"
    ot, oc = costs(a.oracle)
    ut, uc = costs(a.ours)
    rows = []
    for pc in set(oc) & set(uc):
        mo, mu = ot[pc] / oc[pc], ut[pc] / uc[pc]
        rows.append(((mu - mo) * oc[pc], pc, mo, mu, oc[pc], uc[pc]))
    rows.sort()
    total = sum(r[0] for r in rows)
    print(f"net (ours - oracle) cycles over common addresses, weighted by oracle counts: {total:+.0f}")
    for w, pc, mo, mu, n1, n2 in rows[:a.top // 2] + rows[-a.top // 2:]:
        print(f"{pc} {code(pc):8s} oracle {mo:6.2f} ours {mu:6.2f} n={n1}/{n2} weight {w:+.0f}")


if __name__ == "__main__":
    main()
