"""Maintained movie durations and headless executable location; no oracle dependency."""
import os
from pathlib import Path
from paths import ROOT

EVERY = 300
# movie -> (frames, movie whose final save it starts from, or None)
MOVIES = {  # the default suite: the WSC version
    "newgame": (15000, None),
    "story": (16700, None),
    "continue": (3000, "newgame"),
}
# Archived recordings: ORIJINARU (docs/ORIJINARU.md) and the unsafe legacy event sweep.
# Not run by default; retained for explicit replay by name.
ARCHIVED_MOVIES = {
    "orijinaru": (3600, None),
    "orijinaru_play": (16800, None),
}
# Optional developer recordings are absent from public snapshots.
CHAPTERS = {}
ARCHIVED_CHAPTERS = {}
for registry in sorted(Path(__file__).with_name('developer').glob('movies_*.py')):
    import runpy
    recordings = runpy.run_path(str(registry))
    CHAPTERS.update(recordings['CHAPTERS'])
    ARCHIVED_CHAPTERS.update(recordings.get('ARCHIVED_CHAPTERS', {}))
ALL_MOVIES = {**MOVIES, **ARCHIVED_MOVIES, **ARCHIVED_CHAPTERS, **CHAPTERS}
PARTS = ("ram", "sram", "state", "screen")
RUNTIME_PARTS = ("iram", "sram", "screen")
HEADLESS = Path(os.environ.get("SAWGOD_HEADLESS") or ROOT / "build" / "linux-release" / "sawgod-headless")

