#!/usr/bin/env python3
"""Normalize/merge Mesen2 CDLv2 or text opcode logs to hexadecimal-offset decimal-flags rows."""
import argparse
from pathlib import Path
import zlib
from cartridge import load, pairs


def read(path, cart, format="auto"):
    data = Path(path).read_bytes()
    if format == "auto":
        format = "mesen" if data.startswith(b"CDLv2") else "text"
    if format == "text":
        out = pairs(path)
    else:
        if not data.startswith(b"CDLv2") or len(data) != 9 + cart.capacity:
            raise ValueError("expected Mesen2 CDLv2 header and one flag byte per padded ROM byte")
        if int.from_bytes(data[5:9], "little") != zlib.crc32(cart.data):
            raise ValueError("CDL CRC32 does not match ROM")
        # Native WS logs mark operands as Code too. Recover boundaries by decoding each
        # code run, retaining explicit jump/subroutine entry flags even for overlapping code.
        import capstone
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_16)
        flags = data[9 + cart.padding:]
        out = {o: 2 for o, f in enumerate(flags) if f & 2}
        off = 0
        while off < cart.size:
            if not flags[off] & 1:
                off += 1
                continue
            out[off] = out.get(off, 0) | 1
            i = next(md.disasm(cart.data[off:off + 15], off & 0xFFFF, count=1), None)
            length = i.size if i and all(f & 1 for f in flags[off:off + i.size]) else 1
            off += length
        for off, f in enumerate(flags):
            if f & 0x0C:
                out[off] = out.get(off, 0) | 1
    if any(not 0 <= o < cart.size for o in out):
        raise ValueError("CDL offset outside ROM")
    return out


def write(path, rows):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text("".join(f"{o:x} {f}\n" for o, f in sorted(rows.items())), encoding="utf-8", newline="")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("logs", nargs="+", type=Path)
    ap.add_argument("--format", choices=("auto", "text", "mesen"), default="auto")
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    cart, merged = load(a.rom), {}
    for path in a.logs:
        for o, f in read(path, cart, a.format).items():
            merged[o] = merged.get(o, 0) | f
    write(a.out, merged)


if __name__ == "__main__":
    main()
