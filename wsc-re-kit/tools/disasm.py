#!/usr/bin/env python3
"""Generate publishable GNU as source: instructions and labels, .incbin for all opaque bytes."""
import argparse
from pathlib import Path
import re
import shutil
import capstone
from asm_roundtrip import HINTS, assemble, gas_text
from build_rom import build
from cartridge import BANK, load, pairs, symbols

PREAMBLE = '.intel_syntax noprefix\n.code16\n.arch i186\n.include "macros.inc"\n'


ACC = {0x05: "add", 0x0D: "or", 0x15: "adc", 0x1D: "sbb", 0x25: "and", 0x2D: "sub", 0x35: "xor", 0x3D: "cmp"}
REG16 = ("ax", "cx", "dx", "bx", "sp", "bp", "si", "di")


def imm16_macro(i):
    """accw/aluw macro text for a 16-bit ALU op with an imm16 gas would shorten, else None."""
    b = bytes(i.bytes)
    if len(b) == 3 and b[0] in ACC:
        return f"accw {ACC[b[0]]}, 0x{int.from_bytes(b[1:], 'little'):04x}"
    if len(b) == 4 and b[0] == 0x81 and b[1] >= 0xC0:
        op = ("add", "or", "adc", "sbb", "and", "sub", "xor", "cmp")[(b[1] >> 3) & 7]
        return f"aluw {op}, {REG16[b[1] & 7]}, 0x{int.from_bytes(b[2:], 'little'):04x}"
    return None


def pick_hints(rom, md, insns):
    chosen, todo = {}, list(insns)
    for hint in HINTS:
        if not todo:
            break
        rest = []
        for i, encoded in zip(todo, assemble([hint + gas_text(i) for i in todo])):
            if encoded == bytes(i.bytes):
                chosen[i.address] = hint
            else:
                rest.append(i)
        todo = rest
    chosen.update((i.address, None) for i in todo)
    return chosen


def generate(cart, code, out, names=None):
    names = names or {}
    out.mkdir(parents=True, exist_ok=True)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
    total, opaque = 0, 0
    for bank in range(cart.size // BANK):
        base = bank * BANK
        insns, end = [], base
        for off in sorted(o for o in code if base <= o < base + BANK):
            if not 1 <= code[off] <= 15 or off + code[off] > cart.size:
                raise ValueError(f"invalid instruction length at {off:x}")
            if off < end or off + code[off] > base + BANK:
                continue
            i = next(md.disasm(cart.data[off:off + code[off]], off & 0xFFFF, count=1), None)
            if i is None or i.size != code[off]:
                raise ValueError(f"code length does not match decode at {off:x}")
            insns.append(i)
            end = off + i.size
        starts = {i.address for i in insns}
        labels = {o - base: n for o, n in names.items() if base <= o < base + BANK}
        inside = {i.address + k for i in insns for k in range(1, i.size)}
        if inside & labels.keys():
            raise ValueError("symbol points inside an instruction; fix code boundaries first")
        hints = pick_hints(cart.data, md, insns)
        for i in insns:
            match = re.fullmatch(r"(j\w+|call|loop\w*) \. \+ (-?\d+)", gas_text(i))
            if match:
                target = (i.address + int(match[2])) & 0xFFFF
                if target in starts:
                    labels.setdefault(target, f"L_{base + target:06X}")
        lines = [PREAMBLE, f'.section .bank_{bank:02X},"ax"\n']

        def label(pos):
            if pos in labels:
                lines.append(f".globl {labels[pos]}\n{labels[pos]}:\n")

        def data(lo, hi):
            cuts = sorted({lo, hi} | {p for p in labels if lo <= p < hi})
            for x, y in zip(cuts, cuts[1:]):
                label(x)
                lines.append(f'  .incbin "baserom.wsc", 0x{base + x:X}, 0x{y - x:X}\n')

        pos = 0
        for i in insns:
            data(pos, i.address)
            label(i.address)
            text, hint = gas_text(i), hints[i.address]
            if hint is None and imm16_macro(i):
                lines.append(f"  {imm16_macro(i)}\n")
            elif hint is None:
                # Never put unsupported encodings into a .byte directive or a hex-byte comment.
                lines.append(f'  .incbin "baserom.wsc", 0x{base + i.address:X}, {i.size}  # {text}\n')
                opaque += 1
            else:
                match = re.fullmatch(r"(j\w+|call|loop\w*) \. \+ (-?\d+)", text)
                if match:
                    target = (i.address + int(match[2])) & 0xFFFF
                    # Wrapping relative branches remain numeric: bank VMA labels cannot express that wrap.
                    if target in starts and i.address + int(match[2]) == target:
                        text = f"{match[1]} {labels[target]}"
                        if i.bytes[0] == 0xE9:
                            hint = "{disp16} "
                lines.append(f"  {hint}{text}\n")
            pos = i.address + i.size
        data(pos, BANK)
        (out / f"bank_{bank:02X}.s").write_text("".join(lines), encoding="utf-8", newline="")
        total += len(insns)
    return total, opaque


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("code", type=Path)
    ap.add_argument("--symbols", type=Path)
    ap.add_argument("--out", type=Path, required=True)
    a = ap.parse_args()
    cart = load(a.rom)
    names = {}
    for kind, off, name, size in symbols(a.symbols):
        if kind != "ram":
            if off + max(size, 1) > cart.size:
                raise ValueError("ROM symbol outside image")
            names[off] = name
    total, opaque = generate(cart, pairs(a.code), a.out, names)
    (a.out / "include").mkdir(exist_ok=True)
    shutil.copyfile(Path(__file__).resolve().parent.parent / "asm/include/macros.inc", a.out / "include/macros.inc")
    build(a.rom, a.out, a.out)
    print(f"{total} instructions ({opaque} opaque encodings); {a.out / 'rom.wsc'}: byte-identical to input")


if __name__ == "__main__":
    main()
