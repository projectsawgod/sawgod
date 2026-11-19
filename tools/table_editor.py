#!/usr/bin/env python3
"""table_editor.py - a local web editor for the game data tables (docs/DATA_TABLES.md, docs/MODS.md).

  table_editor.py [--mod DIR] [--port 8765]

Serves tools/table_editor.html on http://localhost:PORT. The editor shows the tables as the game's first run
extracts them from the ROM (sawgod-headless --export-tables), with the edits already saved in the mod folder
DIR laid over them. DIR defaults to the game's own mods folder (<user data>/mods/<rom name>/editor), which the
game loads at every start, so a save takes effect the next time the game starts.

Saving writes DIR/tables/<table>.csv for each table that differs from the ROM, holding only what changed (the
changed rows; in the fixed-size tables only the changed cells), so mods stack cell by cell; a table back to
the original loses its file. Pack zips DIR (with a mod.toml) to build/mods/<DIR name>.sgz, to share: players
drop it into their mods folder or run the game with --mod. Nothing is served beyond localhost.
"""
import argparse
import csv
import io
import json
import shutil
import subprocess
import sys
import zipfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from movie_suite import HEADLESS  # noqa: E402
from paths import SAWGOD_ROM, SAWGOD_TMP, SAWGOD_DATA, ROOT  # noqa: E402

PAGE = Path(__file__).resolve().parent / "table_editor.html"
# Pointer tables (variable-length records; game/tables.h): a changed row is written whole, since a blank
# formation group means "no group" there, not "unchanged".
WHOLE_ROWS = {"monsters", "formations"}


def read_csv(text):
    rows = list(csv.reader(io.StringIO(text)))
    return (rows[0], rows[1:]) if rows else ([], [])


def write_csv(head, rows):
    out = io.StringIO()
    csv.writer(out, lineterminator="\n").writerows([head] + rows)
    return out.getvalue()


def override(name, base_text, text):
    """The part of a full table `text` that differs from `base_text`, as an override CSV ("" if none)."""
    head, base = read_csv(base_text)
    head2, rows = read_csv(text)
    if head2 != head:
        raise ValueError(f"{name}: the columns don't match the extracted table")
    by_id = {r[0]: r for r in base}
    changed = [r for r in rows if by_id.get(r[0]) != r]
    if not changed:
        return ""
    if name in WHOLE_ROWS:
        return write_csv(head, changed)
    cols = [k for k in range(1, len(head)) if any(r[k] != by_id[r[0]][k] for r in changed)]
    return write_csv([head[0]] + [head[k] for k in cols],
                     [[r[0]] + [r[k] if r[k] != by_id[r[0]][k] else "" for k in cols] for r in changed])


def merge(base_text, text):
    """`base_text` with an override CSV laid over it (rows by id; blank cells keep the base's)."""
    head, base = read_csv(base_text)
    ohead, rows = read_csv(text)
    by_id = {r[0]: r for r in base}
    for r in rows:
        if r and r[0] in by_id:
            for h, v in zip(ohead[1:], r[1:]):
                if h in head and v != "":
                    by_id[r[0]][head.index(h)] = v
    return write_csv(head, base)


def main():
    rom_name = Path(SAWGOD_ROM).stem
    ap = argparse.ArgumentParser()
    ap.add_argument("--mod", type=Path, default=SAWGOD_DATA / "mods" / "editor",
                    help="mod folder the edits are saved to (DIR/tables/*.csv)")
    ap.add_argument("--port", type=int, default=8765)
    a = ap.parse_args()
    mod = a.mod.resolve()
    # the tables exactly as the game extracts them (never the player's edited copies)
    base_dir = SAWGOD_TMP / "table_editor" / "base"
    shutil.rmtree(base_dir, ignore_errors=True)
    subprocess.run([str(HEADLESS), str(SAWGOD_ROM), "--export-tables", str(base_dir)], check=True)
    current = sorted(f.stem for f in base_dir.glob("*.csv"))

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass

        def send(self, code, body, kind="application/json"):
            data = body if isinstance(body, bytes) else json.dumps(body, ensure_ascii=False).encode("utf-8")
            self.send_response(code)
            self.send_header("Content-Type", kind + "; charset=utf-8")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(data)

        def do_GET(self):
            if self.path in ("/", "/index.html"):
                return self.send(200, PAGE.read_bytes(), "text/html")
            if self.path == "/api/tables":
                tables = {}
                for n in current:
                    base = (base_dir / f"{n}.csv").read_text(encoding="utf-8")
                    edited = mod / "tables" / f"{n}.csv"
                    tables[n] = {"base": base,
                                 "edited": merge(base, edited.read_text(encoding="utf-8")) if edited.exists()
                                 else None}
                return self.send(200, {"tables": tables, "mod": str(mod)})
            self.send(404, {"error": "not found"})

        def do_POST(self):
            body = json.loads(self.rfile.read(int(self.headers.get("Content-Length", 0))) or b"{}")
            if self.path == "/api/save":
                saved, removed = [], []
                for name, text in body.get("tables", {}).items():
                    if name not in current:
                        return self.send(400, {"error": f"unknown table {name}"})
                    out = mod / "tables" / f"{name}.csv"
                    try:
                        part = override(name, (base_dir / f"{name}.csv").read_text(encoding="utf-8"), text)
                    except ValueError as e:
                        return self.send(400, {"error": str(e)})
                    if not part:
                        if out.exists():
                            out.unlink()
                            removed.append(name)
                        continue
                    out.parent.mkdir(parents=True, exist_ok=True)
                    out.write_text(part, encoding="utf-8", newline="")
                    saved.append(name)
                return self.send(200, {"saved": saved, "removed": removed})
            if self.path == "/api/pack":
                toml = mod / "mod.toml"
                if not toml.exists():
                    mod.mkdir(parents=True, exist_ok=True)
                    toml.write_text(f'name = "{mod.name}"\nversion = "1.0"\n', encoding="utf-8", newline="")
                target = ROOT / "build" / "mods" / f"{mod.name}.sgz"
                target.parent.mkdir(parents=True, exist_ok=True)
                files = sorted(f for f in mod.rglob("*") if f.is_file())
                with zipfile.ZipFile(target, "w", zipfile.ZIP_DEFLATED) as z:
                    for f in files:
                        z.write(f, f.relative_to(mod).as_posix())
                return self.send(200, {"ok": True, "file": str(target),
                                       "output": f"{len(files)} files -> {target}"})
            self.send(404, {"error": "not found"})

    server = ThreadingHTTPServer(("127.0.0.1", a.port), Handler)
    print(f"table editor: http://localhost:{a.port}  (edits saved to {mod}; Ctrl+C to stop)")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
