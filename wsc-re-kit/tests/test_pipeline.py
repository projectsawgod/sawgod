#!/usr/bin/env python3
"""Synthetic cartridge integration tests. Every ROM and CDL is generated in a temporary directory."""
import argparse
import importlib.util
from pathlib import Path
import subprocess
import shutil
import sys
import tempfile
import unittest
import zlib

if __name__ == "__main__":
    missing = []
    if importlib.util.find_spec("capstone") is None:
        missing.append("Python capstone")
    for name in ("as", "ld", "objcopy"):
        path = shutil.which(name)
        if not path or "GNU" not in subprocess.run([path, "--version"], capture_output=True, text=True).stdout:
            missing.append("GNU " + name)
    if missing:
        print("Missing kit test requirements: " + ", ".join(missing))
        raise SystemExit(77)

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))
from cartridge import Cartridge, Mapping, ROM_SIZES
from cdl import read, write
from codeflow import discover


def synthetic(size=0x20000, save=2, mapper=1, color=1):
    rom = bytearray(b"\xff" * size)
    top = size - 0x4000
    # FC00:0000: initialize a RAM stack, near call, conditional branch, nonaligned far call, halt.
    rom[top:top + 22] = bytes.fromhex("fa b8 00 00 8e d0 bc 00 20 e8 14 00 75 02 90 90 9a 30 00 00 fc f4")
    rom[top + 0x20:top + 0x24] = bytes.fromhex("b8 01 00 c3")
    rom[top + 0x30:top + 0x34] = bytes.fromhex("b8 02 00 cb")
    rom[top + 0x40:top + 0x44] = bytes.fromhex("e8 0d 00 f4")
    rom[top + 0x50] = 0xC3
    # Long immediate encodings and a duplicate prefix need opaque encoding preservation.
    rom[top + 0x60:top + 0x6B] = bytes.fromhex("05 01 00 81 c3 01 00 26 26 90 c3")
    rom[0x100:0x104] = bytes.fromhex("e8 0d 00 c3")
    rom[0x110] = 0xC3
    rom[-16:-11] = bytes.fromhex("ea 00 00 00 fc")
    rom[-10:-2] = bytes([0, color, 0, 0, next(k for k, v in ROM_SIZES.items() if v == size), save, 0, mapper])
    rom[-2:] = (sum(rom[:-2]) & 0xFFFF).to_bytes(2, "little")
    return bytes(rom)


def run(tool, *args):
    return subprocess.run([sys.executable, str(TOOLS / tool), *map(str, args)], check=True, capture_output=True, text=True)


class Pipeline(unittest.TestCase):
    def test_selected_banks_and_ip_wrap(self):
        cart = Cartridge(synthetic(4 << 20))
        mapping = Mapping(cart, page=1, bank0=3, bank1=9)
        self.assertEqual(mapping.rom(0x40000), 0x140000)
        self.assertEqual(mapping.rom(0x2ABCD), 0x3ABCD)
        self.assertEqual(mapping.rom(0x30010), 0x90010)
        self.assertIsNone(Mapping(Cartridge(synthetic(3 << 20)), page=0).rom(0x40000))
        rom = bytearray(cart.data)
        rom[0x3EFFFE:0x3F0000] = bytes.fromhex("eb 02")
        rom[0x3E0002] = 0xC3
        rom[-2:] = (sum(rom[:-2]) & 0xFFFF).to_bytes(2, "little")
        cart = Cartridge(rom)
        code, _, _ = discover(cart, Mapping(cart), seeds=[(0xE000, 0xFFFE)])
        self.assertEqual(code[0x3EFFFE], 2)
        self.assertEqual(code[0x3E0002], 1)

    def test_geometry_and_contexts(self):
        for size in ROM_SIZES.values():
            with self.subTest(size=size):
                cart = Cartridge(synthetic(size))
                cart.validate()
                self.assertEqual(cart.checksum, cart.computed_checksum)
                mapping = Mapping(cart)
                self.assertEqual(mapping.rom(0xFFFF0), size - 16)
                self.assertEqual(mapping.rom(0xFC000), size - 0x4000)
                self.assertEqual(cart.save_type, "SRAM")
                self.assertEqual(cart.save_size, 32768)
                code, _, _ = discover(cart, mapping, {0x100: 1, size - 0x4000 + 0x60: 1}, [(0xFC00, 0x40)])
                expected = {0: 1, 1: 3, 4: 2, 6: 3, 9: 3, 12: 2, 14: 1, 15: 1, 16: 5, 21: 1,
                            0x20: 3, 0x23: 1, 0x30: 3, 0x33: 1, 0x40: 3, 0x43: 1, 0x50: 1,
                            0x60: 3, 0x63: 4, 0x67: 3, 0x6A: 1}
                self.assertEqual(code, {**{size - 0x4000 + o: n for o, n in expected.items()},
                                        size - 16: 5, 0x100: 3, 0x103: 1, 0x110: 1})

    def test_cdl_formats(self):
        cart = Cartridge(synthetic(3 << 20))
        with tempfile.TemporaryDirectory() as td:
            path = Path(td) / "capture.cdl"
            flags = bytearray(cart.capacity)
            flags[cart.padding + 0x100:cart.padding + 0x103] = bytes([1 | 4 | 8, 1, 1 | 2])
            path.write_bytes(b"CDLv2" + zlib.crc32(cart.data).to_bytes(4, "little") + flags)
            self.assertEqual(read(path, cart), {0x100: 1, 0x102: 2})
            text = Path(td) / "capture.txt"
            write(text, read(path, cart))
            self.assertEqual(read(text, cart), {0x100: 1, 0x102: 2})
            data = bytearray(path.read_bytes()); data[5] ^= 1; path.write_bytes(data)
            with self.assertRaisesRegex(ValueError, "CRC32"):
                read(path, cart)

    def test_pipeline_and_rebuild(self):
        for size in (0x20000, 0x80000, 3 << 20, 4 << 20):
            with self.subTest(size=size), tempfile.TemporaryDirectory(prefix="wsc kit ") as td:
                root = Path(td)
                rom = root / "input.wsc"; rom.write_bytes(synthetic(size))
                cdl = root / "coverage.txt"; write(cdl, {0x100: 1, size - 0x4000 + 0x60: 1})
                code = root / "code.txt"
                run("romid.py", rom, "--strict")
                run("codeflow.py", rom, cdl, "--seed", "FC00:0040", "--out", code)
                names = root / "symbols.txt"
                names.write_text(f"rom {size - 0x4000:x} entry 22\nrom 120 data_label 2\nram 100 work 2\n", encoding="utf-8", newline="")
                out = root / "assembly"
                result = run("disasm.py", rom, code, "--symbols", names, "--out", out)
                self.assertIn("byte-identical", result.stdout)
                self.assertEqual((out / "rom.wsc").read_bytes(), rom.read_bytes())
                source = "".join(p.read_text(encoding="utf-8") for p in out.glob("bank_*.s"))
                self.assertIn("entry:", source)
                self.assertIn("data_label:", source)
                self.assertNotIn(".byte", source)
                self.assertIn(".incbin", source)
                # Rebuild the published source without depending on the generator's object files.
                run("build_rom.py", rom, out, "--out", root / "rebuild")
                self.assertEqual((root / "rebuild/rom.wsc").read_bytes(), rom.read_bytes())

    def test_mapper_and_save_fields(self):
        for save, kind, size in ((0, "none", 0), (1, "SRAM", 8192), (0x10, "EEPROM", 128),
                                 (0x20, "EEPROM", 2048), (0x50, "EEPROM", 1024)):
            cart = Cartridge(synthetic(save=save, mapper=0))
            cart.validate()
            self.assertEqual((cart.save_type, cart.save_size), (kind, size))
        with self.assertRaisesRegex(ValueError, "mapper"):
            Cartridge(synthetic(mapper=9)).validate()
        rom = bytearray(synthetic()); rom[-6] = 6
        with self.assertRaisesRegex(ValueError, "size"):
            Cartridge(rom).validate()


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--write-rom", type=Path, help="write a synthetic ROM for manual Ghidra/Mesen2 checks")
    args, remaining = parser.parse_known_args()
    if args.write_rom:
        args.write_rom.write_bytes(synthetic())
    else:
        unittest.main(argv=[sys.argv[0], *remaining])
