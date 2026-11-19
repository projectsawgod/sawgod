# Contributing to SawGod

SawGod is a native, moddable port of the 2002 WonderSwan Color *Makai Toushi SaGa*, built on a
decompilation of the original game. Start with:

- [Build and test](docs/BUILDING.md): desktop presets, dependencies and ROM-gated tests.
- [Code map](docs/CODE_MAP.md): frame flow, game rules, screens and a C++ hook example.
- [Mods](docs/MODS.md): resource overrides and [data tables](docs/DATA_TABLES.md).
- [Android](docs/ANDROID.md) and [release packaging](docs/RELEASE.md): platform and distribution details.

## The one hard rule: no ROM data in the repository

You need your own copy of the game. The repository must never contain the ROM, the WonderSwan BIOS,
graphics, music, text or any other bytes copied from the cartridge, nor code copied from other emulators.
Decompiled *source* is fine, as in every decomp project; binary data is not. The game extracts what it
needs from your ROM on first run.

Before committing, run `python3 tools/leakscan.py`: it rejects any file containing a run of 32+ bytes that
also occurs in the ROM.

## Code style

- C/C++: `clang-format-18` with the repository's `.clang-format` (CI checks handwritten files in
  `src/` and `tests/`). `*.generated.h` shader outputs are checked by their generator instead:
  `python tools/compile_shaders.py --all --check`.
- Names: snake_case English describing what a thing is or does; the game's own vocabulary where it is
  clear (`meat`, `guild`, `crystal`).
- Decompiled game code mirrors the original function by function and keeps each function's ROM address;
  enhancements go behind options, never silently into the original logic.

## Commits and pull requests

Small, focused commits with a message that says what changed and why. Pull requests need CI green and
passing unit tests (with your own ROM for the ROM-gated ones; say so in the PR). By contributing you agree
to dedicate your contribution to the public domain under CC0 1.0, like the rest of the project
([LICENSE](LICENSE)).
