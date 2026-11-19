# Save files

Saves use the port's own format, not WSC SRAM, for every slot (1-3 included). The
native game keeps every save slot in the **save store**: one readable, versioned text file per slot. Code:
`src/game/save_file.{h,cpp}` (format, store directory, SRAM import for headless runs), `src/game/save_slots.{h,cpp}` (the slots the
game's save routines read and write), `src/app/main.cpp` (where and when files are read and written).

## Location

SDL's per-user preference directory (`SDL_GetPrefPath("sawgod", "sawgod")`, same place as `sawgod.toml`,
docs/CONFIG.md), then `saves/`:

- Linux: `~/.local/share/sawgod/sawgod/saves/`
- Windows: `%APPDATA%\sawgod\sawgod\saves\`
- macOS: `~/Library/Application Support/sawgod/sawgod/saves/`

One store, whatever the ROM file is called.

| File | What |
| --- | --- |
| `slot1.toml` ... `slot9.toml` | One per saved slot (a slot never saved has no file). |
| `auto.toml` | The autosave (slot AUTO), same format. |
| `store.toml` | `format`, `last_slot` (1-9, 0 = none): the slot the save/load list opens on. |

## Slot file format (format 2)

TOML. The file is the game's save payload -- the 0x439-byte game state block the original copies from RAM
(SS:0xD000) to SRAM -- as named fields, with player names stored as full host strings. All fields except
names preserve their payload bytes exactly (coverage checked by `tests/test_save_file.cpp`). Names can
contain up to eight Unicode code points in new name entry. A file may hold up to ten, stored intact, but the
HUD and battle messages display only their first eight glyphs. The first four representable glyphs are
projected into machine memory for the native menus. An optional `host_name = ""` in a `[[member]]`
keeps an explicitly empty host name (use the native name as a fallback); `name` still preserves its RAM
projection. Without that marker, `name` is also the host name, as in earlier format 2 saves. Not in the file:
the 16-bit checksum and the two
"valid save" marker bytes (0x1B, 0xE4), both made when the file loads -- so **a hand-edited file loads as long
as it parses**. Numbers may be decimal or `0x` hex; arrays may span lines; `#` starts a comment.

```toml
format = 2

[party]
gold = 40
inventory = [[255, 255], ...]   # 8 x [item id, uses]; 255 = empty
key_items = [[255, 255], ...]   # 11 x [item id, uses]

[location]
map = 88                        # tables/maps row
x = 19
y = 13
travel_mode = 0
previous_map = 1                # where a field transition returns to
previous_x = 9
previous_y = 37
vehicles = [[102, 32, 31, 0], ...]  # 3 parked vehicles: [map, x, y, state]

[progress]
play_time_frames = 11131        # 75.47 per second
tower_counter = 1
ending_seen = 0
event_flags = "00...00"         # 19 bytes hex: flag n = byte n/8, bit 0x80 >> n%8
bestiary_flags = "00...00"      # 27 bytes hex
packed_progress = "0000000000000000"
progression_bits = 0x01

[config]                        # the game's own Config menu
dialogue_speed = 4
field_options = 0x1F            # bit 0 dash, 1 auto-target, 2 dash button inverted

[dialogue]
leader = 0
party_order = [3, 1, 8, 2]

[unknown]                       # not decoded yet: kept as hex
byte_db = 0x01
byte_e4 = 0x00
bytes_ff = "000000000000"
bytes_437 = "0000"

[[member]]                      # exactly four, in party order
name = "Alexandr"               # real UTF-8 string, eight-glyph entry limit; no padding required
race = 177                      # character type: tables/monsters row
hp = 60
hp_max = 60
strength = 8
defense = 1
agility = 4
magic = 0
equipment = [[33, 45], ...]     # 8 x [item id, uses]; 255 = empty
equipment_flags = 0x83
status = 0x00                   # 0xFF = no member in this place
queues = "0100..."              # 204 bytes hex: the member's icon/token queues
```

A file that doesn't parse -- syntax, a missing or unknown field, a value out of range, another `format` --
is reported on stderr with the line or field, its slot shows as empty, and that file is **never written
over** in that session, so a hand edit with a typo isn't lost. `format` (2) says
which format a file is in.

Names are stored alongside each slot's game state, including AUTO, and restored on load. They follow
party reordering and remain independent of character species. Machine glyph codes with no Unicode
character (from an imported SRAM image, for example) use private-use characters U+E000–U+E0FF to retain their
identity.

## Behavior

- With the native game (the default) every slot -- 1-3 as well as 4-9 -- is in the store. The game never
  reads or writes a save byte of SRAM (not the slots at 0x3301, their backups at +0xCB1, nor the last-used
  byte 0x3300): `load_save_slot`, `write_save_slot`, `read_last_save_slot`, `write_last_save_slot` and the
  title's continue check go to `game::SaveSlots` (`save_store()`), which the app attaches to the store.
- At start the app reads the store; with no `store.toml` yet it starts a new, empty store.
- A slot's file is written as soon as the player saves to it (and `store.toml` when the last-used slot
  changes); anything pending is written again on exit. Every write is atomic (`WriteFileAtomic`: a temporary
  file, flushed, renamed over the old one). A failed write is reported (stderr, and the first time a
  message box), retried 8 s later and on exit; the previous file stays intact.
- `--no-save`: starts with no saves and writes nothing. While replaying `--inputs` the store is read but never
  written (a replay must not overwrite the player's saves).
- **Nine slots are standard**, with AUTO first. The redesigned lists show three rows and scroll without wrapping
  (`src/game/save_ui.cpp`); the storage boundary still maps the selection row to `top + row`.
  Headless/fallback tests can still exercise the original three-row menus.
- **Without a store** -- headless runs (`sawgod-headless` without `--store`), `--native none`
  (the original code) and `--sram FILE` -- slots 1-3 are the original SRAM slots, byte for byte, so the
  equivalence checks compare the original save path. `--native none` reads and writes the raw SRAM image
  `<rom name>.sav` (separate from the store); `--sram FILE` loads a raw image (to replay
  a recording made from it) and writes nothing back.

- **Autosave** (always on in the native app): the first time the party stands still in
  the field on a map other than the last one autosaved, the game state block -- what the game's own save
  writes -- goes into slot **AUTO** (`auto.toml`; game/autosave.h). The game's memory and the last-used slot
  are untouched. AUTO is the first entry of the save and load lists (no number; `AUTO` label in
  the redesigned list), and the lists scroll through all nine manual slots.
  It can be loaded but never saved over: in the save list it shows the latest autosave, and A on it only
  plays the error sound.
  A replay (`--inputs`) runs without it, so recordings see the menus they were made with.

## Tests

`tests/test_save_file.cpp` (field coverage, random payloads round-trip, hand edits and errors, names, store
write/read/import, unreadable files kept); `tests/test_player_names.cpp` (ten-glyph names, malformed UTF-8,
names derived from machine bytes, numbered/AUTO metadata, party swaps and resets); `tests/test_save_slots.cpp`.
Headless: `--store DIR` (attach a store, read at start, changed slots written at the end), `--store-import
SRAM` (a new store imports a raw SRAM image), `--save-slots 9`.
Checked by hand: a save imported from SRAM and continued natively gives the same RAM and picture
as continuing from the SRAM; editing `gold` in the slot file shows in game.

## Known limitation (timing, not the save format)

A frame-indexed input recording (`tests/movies/*.txt`) made under the original interpreted timing **drifts**
almost immediately when replayed under `--native all`, because native code takes no simulated machine time
per call. A recording-driven comparison of a *native* "play newgame, save, reload, continue" session
against the *interpreter's* playthrough of the same recording therefore won't match past the first frame or
two of input: the two runs react to the same button-press frame numbers at different points in their game
state. The save/load mechanism itself (read file -> `wsc.sram` -> run -> write `wsc.sram` -> file) is
format- and code-path-identical between native and interpreted runs, and is checked directly by a round
trip of the raw bytes, independent of gameplay timing.
