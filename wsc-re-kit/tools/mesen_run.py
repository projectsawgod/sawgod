#!/usr/bin/env python3
"""Capture opcode/data coverage in an isolated portable Mesen2 test-runner session."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
from cartridge import load

SETTINGS = {"Debug": {"ScriptWindow": {"AllowIoOsAccess": True}},
            "Preferences": {"EnableAutoSaveState": False, "EnableRewind": False}}


def lua_str(value):
    return json.dumps(str(value), ensure_ascii=False)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("rom", type=Path)
    ap.add_argument("--mesen", type=Path, default=os.environ.get("MESEN"), help="portable Mesen2 executable (or MESEN)")
    ap.add_argument("--frames", type=int, required=True)
    ap.add_argument("--input", action="append", default=[], help="FRAME:buttons (empty buttons releases)")
    ap.add_argument("--out", type=Path, required=True, help="capture directory (cdl.txt, ram.bin, mesen.log)")
    ap.add_argument("--timeout", type=int, default=120)
    a = ap.parse_args()
    if a.mesen is None or not a.mesen.is_file():
        ap.error("provide --mesen or MESEN pointing to a portable Mesen2 executable")
    if a.frames <= 0 or a.timeout <= 0:
        ap.error("frames and timeout must be positive")
    cart = load(a.rom)
    a.out = a.out.resolve()
    a.out.mkdir(parents=True, exist_ok=True)
    capture = a.out / "cdl.txt"
    previous = capture.stat().st_mtime_ns if capture.exists() else None
    changes = []
    buttons_allowed = {"up", "down", "left", "right", "up2", "down2", "left2", "right2", "start", "a", "b", "sound"}
    for spec in a.input:
        frame, separator, buttons = spec.partition(":")
        if not separator or int(frame) < 0 or any(b not in buttons_allowed for b in buttons.split(",") if b):
            ap.error("invalid FRAME:buttons input")
        changes.append(f"[{int(frame)}] = {lua_str(buttons)}")
    cfg = f"CONFIG = {{frames={a.frames}, padding={cart.padding}, inputs={{{', '.join(changes)}}}, out={lua_str(a.out)}}}\n"
    script = a.out / "capture.lua"
    script.write_text(cfg + Path(__file__).with_name("capture.lua").read_text(encoding="utf-8"), encoding="utf-8", newline="")
    # Copy the single-file portable distribution; never clear saves or rewrite settings in an installation.
    with tempfile.TemporaryDirectory(prefix="mesen-", dir=a.out) as td:
        runner = Path(td) / a.mesen.name
        shutil.copy2(a.mesen, runner)
        for companion in a.mesen.parent.iterdir():
            if companion.is_file() and companion.suffix.lower() in (".dll", ".so", ".dylib"):
                shutil.copy2(companion, Path(td) / companion.name)
        (Path(td) / "settings.json").write_text(json.dumps(SETTINGS), encoding="utf-8", newline="")
        with (a.out / "mesen.log").open("w", encoding="utf-8", newline="") as log:
            result = subprocess.run([str(runner), "--testrunner", f"--timeout={a.timeout}", str(a.rom.resolve()), str(script)],
                                    stdout=log, stderr=subprocess.STDOUT, timeout=a.timeout + 15, cwd=td)
        native = list((Path(td) / "Debugger").glob("*.cdl"))
        if len(native) == 1:
            shutil.copyfile(native[0], a.out / "capture.cdl")
    if result.returncode:
        return result.returncode
    if not capture.exists() or capture.stat().st_mtime_ns == previous:
        raise RuntimeError("Mesen2 produced no capture; inspect mesen.log")
    print(a.out / "cdl.txt")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
