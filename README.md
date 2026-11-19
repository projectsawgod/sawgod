# What is SawGod?

**魔界塔士 Sa・Ga / Final Fantasy Legend**: a decompilation and native rebuild of the 2002 WonderSwan Color remake
of the 1989 Game Boy RPG, for Windows, macOS, Linux and Android.

**[sawgod.ws](https://sawgod.ws)** · **[Download](https://github.com/projectsawgod/sawgod/releases/latest)** ·
[Report a problem](https://github.com/projectsawgod/sawgod/issues)

SawGod needs your own Japanese WonderSwan Color ROM of *魔界塔士 Sa・Ga* (Makai Toushi SaGa). No game data is
included, and none will be provided.

## What it does.

- **Three graphics modes, switchable at any time:** Game Boy and WonderSwan Color use the
  original artwork, while Enhanced adds Mode 7 world maps, point lighting, depth of field and bloom.
- **Three soundtracks:** Game Boy, WonderSwan Color, and new human-arranged versions of the original music.
- **A redesigned interface** with room for longer names and item descriptions, and a new English translation
  written for it. The original Japanese text is also available.
- **Quality of life:** nine save slots and autosave, gamepad remapping, fast-forward, optional boosts and cheats.
- **Moddable:** graphics, game data and text are plain resource files you can easily override.

SawGod is not an emulator wrapper: it is built on a function-complete decompilation of the WonderSwan Color game.
It plays like the original unless an option is turned on. The game defaults to Enhanced with some quality-of-life
options.

## Play

Download the package for your system from the
[latest release](https://github.com/projectsawgod/sawgod/releases/latest), unpack it and run it. On first run,
choose your ROM (`.wsc`, `.zip` or `.7z`); SawGod checks it and extracts the game data locally. Each package
includes a README with controls, data locations and platform notes. Android builds **require** a gamepad for now.

## Build, mod and contribute

- [Build and test](docs/BUILDING.md)
- [Mods](docs/MODS.md) and [data tables](docs/DATA_TABLES.md)
- [Code map](docs/CODE_MAP.md)
- [Contributing](CONTRIBUTING.md): the one hard rule is no ROM data in the repository.

## License

SawGod's own code is public domain (CC0 1.0, [LICENSE](LICENSE)): use it for anything, no permission or credit needed. Third-party components keep their own terms; see
[LICENSES.md](LICENSES.md), which also credits the projects that helped build SawGod.

SawGod was made with AI assistance (Claude). The English translation and the Enhanced music arrangements were
made by hand.
