#!/usr/bin/env python3
"""movie.py - the input recordings (tests/movies/*.txt): key changes plus headless directive lines.

A movie is a list of "FRAME buttons" lines (the TAS sense: an input recording, never video); `read_movie` turns it
into 'FRAME:buttons' specs and `movie_args` its directive lines into sawgod-headless options. Shared by the
difftest, golden, coverage and check tools.
"""

DIRECTIVES = ("slot", "poke", "hold")  # headless-only movie lines (movie_args)


def read_movie(path):
    """Movie lines -> 'FRAME:buttons' specs. 'F b*' is a tap: held for 6 frames, then released.

    Lines starting with a directive word are headless options (movie_args), not inputs:
      slot FILE.toml       start with that save slot file as SRAM slot 1 (path relative to the repo root)
      poke ADDR=VAL@FRAME  write one RAM byte before FRAME (debug: progress, party, position)
      hold ADDR=VAL        rewrite a RAM byte every frame (debug: e.g. hold C881=0, no random encounters)

    Playback applies holds then that frame's pokes before run_frame. Real-call difftests inherit this
    entry snapshot and replay future directives in the same order at each hardware frame boundary,
    indexed by the absolute movie frame, just like keys. This lets a trial receive the later enemy
    defeat or menu-state change that lets the recording continue. Random-register and key-fuzz trials
    inherit the entry snapshot but do not replay future RAM directives. Difftest freezes hardware time
    during VBlank/raster-wait setup, IRQ work and return on both sides; only the idle wait advances the clock.
    Native instruction count never selects a directive or advances its schedule.
    """
    specs, base = [], 0
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line or line.split()[0] in DIRECTIVES:
            continue
        if line.startswith("+"):
            base += int(line[1:])
            continue
        f, _, buttons = line.partition(" ")
        f = int(f) + base
        if buttons.endswith("*"):
            specs += [f"{f}:{buttons[:-1]}", f"{f + 6}:"]
        else:
            specs.append(f"{f}:{buttons}")
    return specs


def movie_args(path, root):
    """The movie's directive lines as sawgod-headless options (--slot/--poke/--hold)."""
    args = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        words = raw.split("#", 1)[0].split()
        if len(words) == 2 and words[0] in DIRECTIVES:
            value = str(root / words[1]) if words[0] == "slot" else words[1]
            args += ["--" + words[0], value]
    return args


def movie_end_frame(path, root):
    """Last scheduled key change or RAM poke, including a tap's release."""
    last = max((int(s.split(":", 1)[0]) for s in read_movie(path)), default=0)
    args = movie_args(path, root)
    for flag, value in zip(args[::2], args[1::2]):
        if flag == "--poke" and "@" in value:
            last = max(last, int(value.rsplit("@", 1)[1]))
    return last
