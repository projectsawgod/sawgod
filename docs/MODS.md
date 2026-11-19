# Mods

**The mod format may change** in 1.x releases; a change will be listed in the release notes.

A mod is a **.sgz** (a zip under the game's own extension) -- or a folder with the same layout -- of override
files. Its layout mirrors the resource archives the first run builds from
the player's ROM (`<data>/wsc.sgz`, `<data>/gb.sgz`), and every file in it changes only what it lists. Code: `src/engine/mod_pack.{h,cpp}` (opening mods: the engine knows nothing about the
game), `src/app/main.cpp` (loading them; the game's loaders read the files).

```
cheap_potions.sgz   (or a folder cheap_potions/)
  mod.toml              optional: name, version, author, description
  tables/items.csv      data-table overrides: an id column plus only the columns you change
  tables/monsters.csv   ...any of the tables in docs/DATA_TABLES.md
```

`mod.toml`:

```toml
name = "Cheap Potions"
version = "1.0"
author = "you"
description = "Potions cost 1 gold"
```

`tables/items.csv` (rows not listed, and columns not given, keep their values):

```csv
id,price
0,1
1,2
```

`python3 tools/sgz.py pack cheap_potions/ cheap_potions.sgz` packs a folder; a zip with `cheap_potions/` at
the top works too. To start from the game's own files: `python3 tools/sgz.py unpack <data>/wsc.sgz DIR`,
then keep only what you change.

## Loading

- `sawgod --mod PATH` (repeatable; a .sgz or a folder), then every .sgz and folder in `<data>/mods/`, by
  name. **Later mods override earlier ones**, cell by cell. `<data>` is the game's data folder
  (docs/CONFIG.md): beside the game for a portable install, else e.g. `~/.local/share/sawgod/sawgod/`.
- They layer over the resource archives `<data>/wsc.sgz` and `<data>/gb.sgz`, which the game builds from the
  ROM and rebuilds when they are missing, damaged or from an older format: never edit those, edit a mod.
- At start the app lists each mod with what it loaded, and how many table rows now differ from the ROM. A
  mod that can't be read, a bad row or an unknown column is reported and skipped; the game still starts.

## What a mod can change today

| Folder | What | Reference |
| --- | --- | --- |
| `tables/` | items, shops, monsters, monster types (incl. sprite graphics), formations, encounters, encounter odds, maps, tilesets, meat tables | docs/DATA_TABLES.md; `tools/table_editor.py` writes these |
| `text/ja.csv` | any string of the game (menus, battle messages and templates, names, item descriptions, field dialogue, name-entry sheets), by key; `key,text` rows, only the keys you change | `game/text.h` |
| `text/en.csv` | the same for the English translation (layered over the Japanese) | `game/text.h` |
| `maps/NNN.csv`, `maps/NNN_attributes.csv`, `maps/NNN_metatiles.csv`, `maps/NNN_types.csv` | a field map: 80 x 80 metatile ids (the map with an 8-cell border), 64 x 64 attribute types (0-31), its 256-entry metatile set (first tile, mirror 0-3), and what each attribute type means (word and byte flags: walkability and the like). `sawgod-headless ROM --export-maps DIR --map-previews` also draws each map as `maps/NNN.png`. Packed at load; an edited map that no longer fits moves to free space in its bank (`tables/maps` follows) | `src/game/maps.h` |
| `gb/NNN_metatiles.png`, `gb/NNN_lookup.csv`, `gb/NNN_cells.csv`, `gb/sprites.png`, ... | the GB layer (gb.sgz), for the GB fidelity level (G/L1 down, H/R1 up): per field map the GB metatiles (16 x 16 each, GB shades 0-3), which one stands for each WSC metatile (`wsc,gb,type`) and per-cell picks (`x,y,gb`); the GB sprites, font, monsters and title. Any file replaces gb.sgz's | `src/game/gfx_swap.h`, `src/game/gb_extract.h` |
| `gfx/<name>.png` | graphics: field tilesets, battle and scene graphics, the font... as indexed PNGs (pixel value = color index 0-15, shown as grays: 0 white .. 15 black); keep the picture's size | `src/game/resource_regions.inc` lists them |
| `gfx/font_palettes.json` | the 4 base palettes (12-bit colors, `#RGB`) | |
| `scripts/event_scripts.bin`, `raw/*.bin`, `audio/sound_bank_3D.bin` | field event scripts, map data, sound data and other data not decoded yet, byte for byte; keep the size | |

### Resources over ROM: what the game reads from files

The first run builds `<data>/wsc.sgz` with: the data tables (`tables/`), the text
(`text/ja.csv`) and 72 **resource regions** (`game/resources.h`, listed in `src/game/resource_regions.inc`,
generated from `assets/manifest/*.yaml`): every graphics bank as PNG, the
palettes, the event scripts, the map, sound and other data as .bin -- 90% of the ROM. The app decodes each
file (a mod's file over it) into the data the game reads there, so pictures, palettes, scripts and data come
from the files. `sawgod-headless --rom-reads FILE` lists every ROM byte a run reads (the code's data reads
and DMA) that no file covers: on the story recording 211 of 133,933 bytes, all small constant tables inside
the code banks (cursor positions, layouts), which stay ROM-backed. Formats not decoded yet (.bin)
are edited byte for byte; a region's file must keep its size (a tile sheet its tile count).

The `.gsa` byte pack (`--assets`, engine/assets.h) is only an import path for
third-party ROM patches (Tower Reversed's IPS).
