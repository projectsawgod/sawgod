#!/usr/bin/env python3
"""demo_override.py - build the M6 demo override pack from the user's ROM: the font/window palettes recoloured
(text amber) and one word replaced in the menu strings (けってい "confirm" -> オーケー "OK", same length).

  demo_override.py [--out build/mods/demo.gsa]

The edited source files are generated under build/mods/demo_src/ (they're ROM-derived, so never committed).
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from paths import ROOT  # noqa: E402


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", type=Path, default=ROOT / "build" / "mods" / "demo.gsa")
    a = ap.parse_args()
    extracted = ROOT / "build" / "assets"
    subprocess.run([sys.executable, str(ROOT / "tools" / "assets.py"), "extract", "--out", str(extracted)],
                   check=True, stdout=subprocess.DEVNULL)
    src = ROOT / "build" / "mods" / "demo_src"
    (src / "gfx").mkdir(parents=True, exist_ok=True)
    (src / "text").mkdir(parents=True, exist_ok=True)
    pal = json.loads((extracted / "gfx" / "font_palettes.json").read_text(encoding="utf-8"))
    for p in pal["palettes"]:
        p[2] = "#FD4"  # colour 2: text and window frames
    (src / "gfx" / "font_palettes.json").write_text(json.dumps(pal, ensure_ascii=False, indent=1), encoding="utf-8")
    text = json.loads((extracted / "text" / "menu_strings.json").read_text(encoding="utf-8"))
    text["strings"] = [s.replace("けってい", "オーケー") for s in text["strings"]]
    (src / "text" / "menu_strings.json").write_text(json.dumps(text, ensure_ascii=False, indent=1), encoding="utf-8")
    subprocess.run([sys.executable, str(ROOT / "tools" / "assets.py"), "pack", str(src), "--out", str(a.out)], check=True)


if __name__ == "__main__":
    main()
