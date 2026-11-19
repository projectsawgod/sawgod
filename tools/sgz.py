#!/usr/bin/env python3
"""sgz.py - unpack and pack .sgz archives (the game's resources and mods; src/engine/sgz.h).

  sgz.py unpack ARCHIVE DIR     write every file of ARCHIVE under DIR
  sgz.py pack DIR ARCHIVE       zip DIR's files into ARCHIVE (a mod: drop it into <data>/mods/)
  sgz.py list ARCHIVE           the files and the stamp

A .sgz is a plain zip. The game builds <data>/wsc.sgz and gb.sgz from the player's ROM on first run; start a
mod by unpacking wsc.sgz, keep only the files you change, and pack the folder (or leave it as a folder in
mods/). A packed mod carries no sgz.json stamp: stamps belong to the base archives only.
"""
import argparse
import sys
import zipfile
from pathlib import Path


def unpack(archive, out):
    with zipfile.ZipFile(archive) as z:
        z.extractall(out)
        print(f"sgz: {len(z.namelist())} files -> {out}")


def pack(folder, archive):
    files = sorted(p for p in folder.rglob("*") if p.is_file() and p.name != "sgz.json")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as z:
        for p in files:
            z.write(p, p.relative_to(folder).as_posix())
    print(f"sgz: {len(files)} files -> {archive}")


def list_files(archive):
    with zipfile.ZipFile(archive) as z:
        if "sgz.json" in z.namelist():
            print(z.read("sgz.json").decode("utf-8").strip())
        for name in sorted(z.namelist()):
            print(name)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    u = sub.add_parser("unpack")
    u.add_argument("archive", type=Path)
    u.add_argument("dir", type=Path)
    p = sub.add_parser("pack")
    p.add_argument("dir", type=Path)
    p.add_argument("archive", type=Path)
    l = sub.add_parser("list")
    l.add_argument("archive", type=Path)
    a = ap.parse_args()
    if a.cmd == "unpack":
        unpack(a.archive, a.dir)
    elif a.cmd == "pack":
        pack(a.dir, a.archive)
    else:
        list_files(a.archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
