#!/usr/bin/env python3
"""check_demo.py - the M6 override check: run the newgame movie to character select (frame 2700) with and
without the demo pack (tools/mods/demo_override.py) over the base archive; the screens must differ, including
the footer line where the replaced word is."""
import subprocess
import sys
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from movie_suite import HEADLESS  # noqa: E402
from movie import read_movie  # noqa: E402
from paths import SAWGOD_ROM, SAWGOD_TMP, ROOT  # noqa: E402


def main():
    work = SAWGOD_TMP / "m6"
    work.mkdir(parents=True, exist_ok=True)
    inputs = work / "newgame.inputs"
    inputs.write_text("\n".join(f"{int(s.split(':')[0])} {s.split(':', 1)[1]}"
                                for s in read_movie(ROOT / "tests" / "movies" / "newgame.txt")) + "\n", encoding="utf-8")
    shots = {}
    for tag, mods in (("vanilla", []), ("demo", [ROOT / "build" / "sawgod.gsa", ROOT / "build" / "mods" / "demo.gsa"])):
        cmd = [str(HEADLESS), str(SAWGOD_ROM), "--frames", "2700", "--inputs", str(inputs), "--shot", str(work / f"{tag}.png")]
        for m in mods:
            cmd += ["--mod", str(m)]
        subprocess.run(cmd, check=True)
        shots[tag] = Image.open(work / f"{tag}.png").convert("RGB")
    a, b = shots["vanilla"], shots["demo"]
    changed = sum(1 for p, q in zip(a.getdata(), b.getdata()) if p != q)
    footer = sum(1 for y in range(128, 144) for x in range(224) if a.getpixel((x, y)) != b.getpixel((x, y)))
    print(f"demo pack: {changed} pixels changed ({footer} in the footer line)")
    if not changed or not footer:
        sys.exit("demo pack did not change the screen as expected")


if __name__ == "__main__":
    main()
