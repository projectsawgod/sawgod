#!/usr/bin/env python3
"""asm_roundtrip.py - check that GNU as (.code16, Intel syntax) reassembles ROM code byte-for-byte.

Linear-sweeps a ROM region with capstone, emits each instruction as gas source (trying the
{load}/{store}/{disp8}/{disp16} encoding hints when the plain form assembles differently),
assembles everything in one `as` run and compares the listing bytes with the ROM.

Usage: asm_roundtrip.py ROM ROM_OFFSET LENGTH [--vaddr SEG_OFFSET]
"""
import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

import capstone

HINTS = ("", "{load} ", "{store} ", "{disp8} ", "{disp16} ")
BRANCH = re.compile(r"^(j\w+|call|loop\w*|jcxz)$")


def gas_text(insn):
    """capstone Intel text -> gas Intel text for one instruction at its own address."""
    m, ops = insn.mnemonic, insn.op_str
    if BRANCH.match(m) and re.fullmatch(r"0x[0-9a-f]+|\d+", ops):
        disp = (int(ops, 0) - insn.address + 0x8000) % 0x10000 - 0x8000  # 16-bit wraparound
        return f"{m} . + {disp}"
    m = {"cwde": "cbw", "cdq": "cwd"}.get(m, m)  # capstone names these by their 32-bit forms
    if m in ("ljmp", "lcall"):
        if "[" not in ops:  # direct: capstone "lcall 0x4000, 0x279" is valid gas as-is
            return f"{m} {ops}"
        return f"{m[1:]} {ops.replace('word ptr', 'dword ptr')}"  # indirect far: m16:16 operand
    ops = re.sub(r"\b(byte|word|dword) ptr ([cdefgs]s):\[", r"\1 ptr \2:[", ops)
    return f"{m} {ops}".strip()


def assemble(lines):
    """Assemble lines; return the byte string produced by each (None if as rejects it)."""
    result = _assemble_once(lines)
    if isinstance(result, set):  # error lines: drop them and retry the rest
        keep = [i for i in range(len(lines)) if i not in result]
        sub = assemble([lines[i] for i in keep]) if keep else []
        out = [None] * len(lines)
        for i, b in zip(keep, sub):
            out[i] = b
        return out
    return result


def _assemble_once(lines):
    with tempfile.TemporaryDirectory() as td:
        src = Path(td, "t.s")
        body = [".intel_syntax noprefix", ".code16", ".arch i186"]
        for i, ln in enumerate(lines):
            body += [f".section .s{i},\"ax\"", ln]
        src.write_text("\n".join(body) + "\n", encoding="utf-8", newline="")
        r = subprocess.run(["as", "--32", "-al=" + str(Path(td, "l.txt")), "-o", str(Path(td, "t.o")), str(src)],
                           capture_output=True, text=True)
        errs = {(int(m.group(1)) - 5) // 2 for m in re.finditer(r"t\.s:(\d+): Error", r.stderr)}
        if errs:
            return errs
        out = [b"" for _ in lines]
        listing = Path(td, "l.txt").read_text(encoding="utf-8", errors="replace") if Path(td, "l.txt").exists() else ""
        for ln in listing.splitlines():  # "  12 0004 8BD8 \tmov..." or continuation "  12      40"
            m = re.match(r"^\s*(\d+) (?:[0-9a-f]{4} |     )([0-9A-F]+)", ln)
            if not m:
                continue
            srcline = int(m.group(1))
            idx = (srcline - 4) // 2  # 3 header lines, then (section, insn) pairs
            if 0 <= idx < len(lines) and (srcline - 4) % 2 == 1:
                out[idx] += bytes.fromhex(m.group(2))
        return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom", type=Path)
    ap.add_argument("offset", type=lambda s: int(s, 0))
    ap.add_argument("length", type=lambda s: int(s, 0))
    ap.add_argument("--vaddr", type=lambda s: int(s, 0), default=None)
    a = ap.parse_args()
    rom = a.rom.read_bytes()
    code = rom[a.offset:a.offset + a.length]
    base = a.vaddr if a.vaddr is not None else a.offset & 0xFFFF
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    insns = list(md.disasm(code, base))
    todo = list(range(len(insns)))
    chosen = {}
    for hint in HINTS:
        if not todo:
            break
        got = assemble([hint + gas_text(insns[i]) for i in todo])
        rest = []
        for i, b in zip(todo, got):
            (chosen.__setitem__(i, hint) if b == bytes(insns[i].bytes) else rest.append(i))
        todo = rest
    covered = sum(len(i.bytes) for i in insns)
    print(f"{len(insns)} instructions, {covered}/{len(code)} bytes decoded; "
          f"{len(insns) - len(todo)} reassemble exactly "
          f"({sum(1 for h in chosen.values() if h)} needed an encoding hint), {len(todo)} do not")
    acc16 = [i for i in todo if insns[i].bytes[0] in (0x05, 0x0D, 0x15, 0x1D, 0x25, 0x2D, 0x35, 0x3D)]
    print(f"  of those, {len(acc16)} are accumulator-imm16 forms with a small immediate (need a macro); "
          f"the rest need .byte")
    for i in todo[:40]:
        n = insns[i]
        print(f"  {n.address:04x} {bytes(n.bytes).hex():14} {gas_text(n)}")
    return 1 if todo else 0


if __name__ == "__main__":
    sys.exit(main())
