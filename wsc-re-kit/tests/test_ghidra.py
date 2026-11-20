#!/usr/bin/env python3
"""Optional end-to-end Ghidra test; set GHIDRA_HOME to enable it."""
import os
import importlib.util
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    home = os.environ.get("GHIDRA_HOME")
    if not home:
        print("Set GHIDRA_HOME for the optional Ghidra check")
        return 77
    if importlib.util.find_spec("capstone") is None:
        print("Missing kit test requirement: Python capstone")
        return 77
    from test_pipeline import synthetic, TOOLS
    from cartridge import Cartridge, Mapping
    from cdl import write
    from codeflow import discover
    kit = TOOLS.parent
    env = dict(os.environ, PYTHON=sys.executable)
    for size, save in ((0x20000, 1), (3 << 20, 0x10)):
        with tempfile.TemporaryDirectory(prefix="wsc-ghidra-") as td:
            root = Path(td)
            rom = root / "synthetic.wsc"
            rom.write_bytes(synthetic(size, save=save))
            cart = Cartridge(rom.read_bytes())
            code, _, _ = discover(cart, Mapping(cart))
            write(root / "cdl.txt", {o: 1 for o in code})
            (root / "symbols.txt").write_text(f"rom {size - 0x4000:x} startup 22\nfunc {size - 0x4000 + 0x20:x} near_helper 4 # callable routine\nram 100 frame_counter 2\n", encoding="utf-8", newline="")
            commands = [
                ["sh", kit / "ghidra/import.sh", rom, root / "project", "synthetic"],
                ["sh", kit / "ghidra/cdl.sh", rom, root / "cdl.txt", root / "project", "synthetic"],
                ["sh", kit / "ghidra/symbols.sh", root / "symbols.txt", root / "project", "synthetic"],
                [Path(home) / "support/analyzeHeadless", root / "project", "synthetic", "-process", "-noanalysis",
                 "-scriptPath", str(kit / "ghidra") + ";" + str(kit / "tests"), "-postScript", "ValidateMap.java",
                 str(size), "sram" if save < 0x10 else "eeprom"],
            ]
            for command in commands:
                result = subprocess.run(list(map(str, command)), env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                        text=True, timeout=120)
                # Headless can exit zero after a Java script error; inspect its report too.
                if result.returncode or "ERROR" in result.stdout:
                    print(result.stdout)
                    raise AssertionError("Ghidra command failed")
            if "ValidateMap: mapping, code, and symbols passed" not in result.stdout:
                raise AssertionError(result.stdout)
    print("Ghidra synthetic mappings, CDL, and symbols passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
