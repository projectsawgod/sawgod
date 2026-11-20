#!/usr/bin/env python3
"""Assemble a bank_XX.s directory with GNU binutils; compare the result with the input ROM."""
import argparse
from pathlib import Path
import shutil
import subprocess
from cartridge import load

INCLUDE = Path(__file__).resolve().parent.parent / "asm" / "include"


def assemble_bank(src, out, include=INCLUDE):
    obj = out / (src.stem + ".o")
    subprocess.run(["as", "--32", "-I", str(out), "-I", str(include), "-o", str(obj), str(src)], check=True)
    return obj


def link(objs, banks, out):
    body = "".join(f"    .bank_{b:02X} {{ *(.bank_{b:02X}) }}\n" for b in banks)
    script = out / "rom.ld"
    script.write_text(f"OUTPUT_FORMAT(elf32-i386)\nSECTIONS {{\n  OVERLAY 0 : AT(0) {{\n{body}  }}\n}}\n",
                      encoding="utf-8", newline="")
    subprocess.run(["ld", "-m", "elf_i386", "-T", str(script), "-o", str(out / "rom.elf"),
                    *map(str, objs)], check=True)
    subprocess.run(["objcopy", "-O", "binary", str(out / "rom.elf"), str(out / "rom.wsc")], check=True)
    return (out / "rom.wsc").read_bytes()


def build(rom, source, out, include=INCLUDE):
    cart = load(rom)
    source, out = Path(source).resolve(), Path(out).resolve()
    if Path(rom).resolve() == out / "rom.wsc":
        raise ValueError("output rom.wsc would overwrite the input ROM; choose another output directory")
    out.mkdir(parents=True, exist_ok=True)
    base = out / "baserom.wsc"
    if Path(rom).resolve() != base:
        shutil.copyfile(rom, base)
    banks = range(cart.size // 0x10000)
    objs = [assemble_bank(source / f"bank_{b:02X}.s", out, include) for b in banks]
    image = link(objs, banks, out)
    if image != cart.data:
        raise ValueError("rebuilt image differs from input ROM")
    return image


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("source", type=Path)
    ap.add_argument("--out", required=True, type=Path)
    a = ap.parse_args()
    build(a.rom, a.source, a.out)
    print(f"{a.out / 'rom.wsc'}: byte-identical to input")


if __name__ == "__main__":
    main()
