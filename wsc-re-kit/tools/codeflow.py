#!/usr/bin/env python3
"""Recursive descent from CDL opcode starts, reset, and explicit segment:offset seeds."""
import argparse
from collections import Counter
from pathlib import Path
import re
import capstone
from cartridge import Mapping, load, mapping_args
from cdl import read, write

STOP = {"ret", "retf", "iret", "jmp", "ljmp", "hlt", "int3"}
NEAR = re.compile(r"^(j\w+|call|loop\w*)$")
TABLE_LOAD = re.compile(r"^mov (\w+), word ptr cs:\[(\w+) \+ (0x[0-9a-f]+|\d+)\]$")


def discover(cart, mapping, cdl=None, seeds=(), jump_tables=False):
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    flags = cdl or {}
    work = [(0xFFFF, 0, mapping)]  # CPU reset vector FFFF:0000
    for off, f in flags.items():
        if not f & 1:
            continue
        linear = mapping.cpu(off)
        context = mapping
        if mapping.rom(linear) != off:
            context = Mapping(cart, mapping.page, (off + cart.padding) >> 16, mapping.bank1)
        work.append((linear >> 4 & 0xF000, linear & 0xFFFF, context))
    work.extend((cs, ip, mapping) for cs, ip in seeds)
    code, tables, odd, seen = {}, {}, Counter(), set()
    while work:
        cs, ip, context = work.pop()
        last = None
        while (cs, ip, context.page, context.bank0, context.bank1) not in seen:
            seen.add((cs, ip, context.page, context.bank0, context.bank1))
            off = context.rom((cs << 4) + ip)
            if off is None:
                odd["target in RAM, absent ROM, or padding"] += 1
                break
            # Fetch through the mapper, including IP wrap and a physical bank boundary.
            offsets = [context.rom((cs << 4) + ((ip + k) & 0xFFFF)) for k in range(15)]
            raw = bytes(cart.data[o] for o in offsets if o is not None)
            i = next(md.disasm(raw, ip, count=1), None)
            if i is None or offsets[:i.size] != list(range(off, off + i.size)):
                odd["undecodable or noncontiguous instruction"] += 1
                break
            if off in code and code[off] != i.size:
                odd["conflicting decode contexts"] += 1
                break
            code[off] = i.size
            m = i.mnemonic
            if NEAR.fullmatch(m) and re.fullmatch(r"0x[0-9a-f]+|\d+", i.op_str):
                work.append((cs, int(i.op_str, 0) & 0xFFFF, context))
            elif m in ("lcall", "ljmp") and "[" not in i.op_str:
                seg, target = (int(x, 0) for x in i.op_str.replace(":", ",").split(","))
                work.append((seg, target, context))
            elif NEAR.fullmatch(m) or m in ("lcall", "ljmp"):
                found = False
                match = TABLE_LOAD.fullmatch(last) if jump_tables and last else None
                if match and i.op_str == match[1]:
                    table = int(match[3], 0)
                    start = context.rom((cs << 4) + table)
                    count = 0
                    for k in range(256 if start is not None else 0):
                        e = context.rom((cs << 4) + ((table + k * 2) & 0xFFFF))
                        if e is None or e + 2 > cart.size:
                            break
                        target = int.from_bytes(cart.data[e:e + 2], "little")
                        t = context.rom((cs << 4) + target)
                        if t is None or (start <= t < e + 2) or t == e:
                            break
                        if flags.get(e, 0) & 2 and not flags.get(t, 0) & 1:
                            break
                        if next(md.disasm(cart.data[t:t + 15], target, count=1), None) is None:
                            break
                        work.append((cs, target, context))
                        count += 1
                    if count:
                        tables[start] = count
                        found = True
                odd["heuristic jump table" if found else "indirect target unknown"] += 1
            if m.startswith("out"):
                odd["I/O write; mapper changes are not simulated"] += 1
            last = f"{m} {i.op_str}"
            if m in STOP:
                break
            ip = (ip + i.size) & 0xFFFF
    return code, tables, odd


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("cdl", nargs="?", type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--seed", action="append", default=[], help="additional entry CS:IP, hexadecimal")
    ap.add_argument("--jump-tables", action="store_true", help="opt in to speculative CS-relative word tables")
    mapping_args(ap)
    a = ap.parse_args()
    cart = load(a.rom)
    seeds = [tuple(int(x, 16) for x in s.split(":")) for s in a.seed]
    if any(len(s) != 2 or any(not 0 <= x <= 0xFFFF for x in s) for s in seeds):
        ap.error("seeds must be CS:IP with two 16-bit hexadecimal values")
    code, tables, odd = discover(cart, Mapping(cart, a.linear_page, a.bank0, a.bank1),
                                 read(a.cdl, cart) if a.cdl else {}, seeds, a.jump_tables)
    write(a.out, code)
    write(a.out.with_name("tables.txt"), tables)
    print(f"{len(code)} instructions, {sum(code.values())} code bytes -> {a.out}")
    for message, count in sorted(odd.items()):
        print(f"  note: {count} x {message}")


if __name__ == "__main__":
    main()
