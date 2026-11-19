# Game data tables

The game's data tables, extracted as human-readable CSV for modding: one row per record, named columns,
decimal numbers, names in the game's own characters. Defaults are byte-for-byte the ROM.

The game's first run extracts them from the player's ROM into `<data>/wsc.sgz` as `tables/<name>.csv` and
reads them from there (Runtime, below; `tools/sgz.py unpack` to read them); a mod changes them with its own
`tables/<name>.csv` (docs/MODS.md).

**Easiest: the table editor** (local web page):

```bash
python3 tools/table_editor.py            # then open http://localhost:8765
```

Every table in tabs, with names shown next to every id that refers to another table (gear and shop items,
the monsters in a formation, what each encounter slot brings), changed cells highlighted and values checked as
you type. Save writes only what changed to a mod folder (`--mod DIR`; default `<data>/mods/editor`,
which the game loads at start); Save & zip also packs it as `build/mods/<folder>.sgz` to share.

**Or by hand:** a CSV with an `id` column and only the columns you change, rows matched by `id`; blank cells
keep their values (any spreadsheet or text editor; keep it UTF-8). Put it in a mod's `tables/` folder. Columns
named `byteN` / `*_rest` / `unknown_*` aren't identified yet: change them only to experiment. Read-only columns
(`name`, `*_names`: text resources) are ignored when loading.

| Table | Rows | Columns | Where in the ROM |
|---|---|---|---|
| `tables/items` | 256 items (weapons, armor, books, consumables...) | `name` (7 characters), `description` (26 characters), `price` (gold), `uses` (when new), `power` (damage multiplier, or an equipped item's defense bonus), `action` (battle handler), `target` (top 3 bits: target kind), `flags` (bit 0: counted uses; 40h/80h: a passive item), `passive_bits`, `byte6` (not identified), `byte7_rest` (not identified), `ai_target_flag` | records 0x3943E0 (8 bytes), names 0x394BE0 (8), descriptions 0x3953E0 (27), prices 0x34C127 (4) |
| `tables/monsters` | 200 monster types | `hp`, `strength`, `defense`, `agility`, `magic`, `gear` (item ids, space-separated; `gear_count` must match; the game uses at most 8), `gear_names` and `name` (read-only), `gold` (reward per enemy), `meat_drop` (0-7: chance of meat after a win, value x 10 out of 70), `unknown_rank_bits`, `esper_or_mecha`, `is_monster_row`, `byte1` (not identified) | pointers 0x36D1FB, records 0x36D38B (variable length) |
| `tables/monster_types` | 200 types (rows match `tables/monsters`: 0-149 monsters, 150-199 human/mutant classes...) | `name` (read-only), `rank` and `family` (meat), `graphics` (sprite set, low byte, and palette, high byte: field sprites), `descriptor` (low byte: the large creation graphic and the battle formation layer's tables; high byte: the layer's second entry) | names 0x396FE0 (9 bytes), 0x3976E8, 0x397878, 0x397A08, 0x397AD0 |
| `tables/shops` | 22 shops | `items` (up to 10 item ids, space-separated), `items_names` (read-only); prices are in `tables/items` | 0x34C527 (10 bytes each); shop 22, the debug shop, is `tables/debug_shop_items` (raw) |
| `tables/encounters` | 141 encounter tables | `chance` (per step, of 256), then the formation for each of 7 slots, columns named by their odds (50%, 25%, 12.5%, 6.25%, 3.1%, 1.6%, 1.6%) | 0x350C3C (8 bytes) |
| `tables/encounter_odds` | 1 | the 7 slot thresholds and the 4 travel-mode chance modifiers | 0x350C31, 0x350C38 |
| `tables/formations` | 256 formations (the ids `tables/encounters` and scripts start) | `cant_run`, then up to 3 groups: `groupN_type` (a `tables/monster_types` id), `groupN_name` (read-only), `groupN_min`/`groupN_max` (0-15, the count is random between them); empty groups blank | pointers 0x3811C8, records 0x3813C8 (repacked on rebuild; all 256 must fit 0x460 bytes) |
| `tables/maps` | 211 field maps | per terrain kind (0-2): `terrainN_encounters` (a `tables/encounters` id, 255 = none) and `terrainN_variant` (battle background set); `song`; map data `tileset`, `data_bank`, `data0-3`; `running_allowed` | 0x35A830 (18 bytes) |
| `tables/tilesets` | 60 field tilesets (the `tileset` of `tables/maps`) | `tile_count`, the offset of its `tiles` in ROM bank id / 4 (`gfx/tilesets_bank_NN.png`), and of its `palettes` (4) and metatile palette `attributes` in bank 0x0F | 0x35B718 (8 bytes) |
| `tables/meat_pairs` | 25 x 29 cells | result row for a pair of families (255 = none) | 0x397B98 |
| `tables/meat_results` | 25 x 16 | the monster type a transformation gives, by result row and the higher rank | 0x397E6D |

Monster gear lists can grow or shrink freely (the game packs the records itself: Pointer tables under
Runtime).

Meat lookup (lookup_meat_transformation, 0x3411A0): the two monsters' families pick a `meat_pairs` cell, the
higher of their ranks picks the `meat_results` column. Which monster is the eater isn't pinned down yet, and
families run to 29 against 29 columns, so some families must be screened out before the lookup.

**Classes and races** have no table of their own: character creation picks a character type (race x 30 +
class x 6 + choice; types above 149 are always available, monster types unlock with event flags), and a
member is built from that type's template like a monster's. So a class's name is in `tables/monster_types`
and its HP, stats and starting gear in `tables/monsters` (rows 150-199).

The extraction the game uses is C++ (`game::export_csv`; `sawgod-headless --export-tables DIR` writes the same
files). A Python pipeline (tools/sawgod/tables.py, assets/manifest/data_tables.yaml, `assets.py
check`) round-trips the ROM as a check; mods use the CSVs, not its `.gsa` packs.

## Native runtime and limits

`Session::tables()` owns the CSV-loaded rows. Mods layer partial rows by `id`; blank cells preserve current
values. Named `table_u8` and address-adapter `table_at`/`table_at16` reads use the store, with ROM fallback
when no table was loaded. The app extracts missing base CSVs into its user resource directory without
replacing existing edits. Headless equivalence runs normally leave this store empty.

**ROM/importer limits:** the row counts, character lengths, addresses and packing sizes in the survey table
above describe the source ROM and the Python round-trip importer. They are not native text or mod asset budgets.
In particular the ROM formation pack's 0x460 bytes and item name/description lengths do not cap native mods.
Text resources use real strings and the host UI's fit budgets.

**Current native adapter limits:** gameplay IDs are still bytes: 256 items and formations, 200 monster
classes, 22 ordinary shops plus the special debug shop. Battle storage permits three enemy groups and eight
gear slots. Variable monster/formation records are owned vectors, packed for lifted address-based reads;
those adapters currently require the pack to fit below offset FFFFh (about 11.1 KB for monsters, 59 KB for
formations). These are implementation limits of the current native adapters, not requirements for future
host mod formats. Native mods need no free space in the original ROM record region. Map rebuilding still
uses the existing bank adapters; keep ROM-import/round-trip constraints distinct from host resource design.

## Battle action preview mapping

`battle_item_details` in `src/game/ui_items.cpp` reads the loaded `tables/items` through `table_u8`
(including mod overrides). It supplies the enemy-target panel's third row; descriptions never enter it.
Cross-checked against these decompiled handlers:

- `passive_bits` is an attack-kind mask **only for actions 7, 26, 27, 31, 34 and 48**:
  `execute_element_slayer_weapon_action`, `execute_elemental_magic_action`, `execute_status_magic_action`,
  `execute_physical_status_action`, `execute_innate_magic_damage_action`, and
  `execute_immunity_checked_status_action` in `bank36_misc.cpp`. The mask is checked against an actor's
  element/status resistance by those handlers, `prepare_reflected_battle_effect`, or
  `check_battle_effect_immunity`. Single-bit labels: 01h Fire, 02h Ice, 04h Thunder, 08h Poison,
  10h Stone, 20h Status, 80h Quake. 20h cannot distinguish sleep, paralysis, death and confusion.
  40h has no reliable label and is omitted. Multiple known bits use **Multiple**: a compact summary
  of multiple attack kinds, not a claim that every kind applies. King's Sword's FFh includes all seven
  named kinds above plus unidentified 40h; no single element describes it honestly.
- Other actions omit the element. Examples: species slayers (8), HP drain (9), scripture (28),
  firearms/ordnance (17–23), martial arts (29), multi-hit (30), and stat reduction (32) use field 5
  as a species mask, damage, hit count, or another parameter. A healing item's magnitude is not Fire/Stone.
- `target & E0h`: 00h Ally, 40h Self, 80h Ranged / One group, A0h Melee / One group,
  C0h Ranged / All enemies. Range is meaningful only for enemy actions: 20h selects melee;
  a clear bit on a healing/guard item does not imply a ranged attack. Other encodings are omitted.
  This follows `battle_target_menu_input_core` (target byte halved into the action bucket) and
  `execute_order_entry_core` (single/group/all scope) in `bank36_battle.cpp`. Labels describe the
  selectable attack's nominal targeting, not the number of creatures an individual hit damages.
- No-action items and special command IDs FCh–FFh have no data preview. Unknown fields are omitted.

Every word and the separator is a `ui_battle_preview_*` host string (Japanese and English defaults in
`text.h`). The existing fonts have no middle dot, so English joins fields with ` / ` and Japanese with
`／`. The translation fit checker measures the composed row with its translated companion fields,
not each short word in isolation.

## Identified and unused `byteN` columns

Found by tracing every decompiled routine (`src/game/*.cpp`) that reads each field, cross-checked against the
extracted CSV values. Still genuinely unidentified fields keep a `byteN` / `unknown_...` name rather than a
guessed one.

**`tables/items` record** (flags, target, action, uses, power, then):
- `passive_bits` -- context-dependent, read only through the shared accessor `lookup_table_byte` (rom
  0x36526E, bx=5): for a status-inflicting battle item (a flags-40h skill id) it's OR'd into the target
  actor's status byte (actor +0Bh) as a status-immunity mask (`apply_battle_status_effect_core`, shared body
  of rom 0x366FFD; `init_enemy_group_actors_core`, rom 0x3625A9, builds the OR for a whole enemy group).
  For a flags-80h item it's OR'd into actor +0Ch instead, an element-resistance mask read back by
  `read_target_element_resistances_core` (rom near 0x364FBE). For an equippable item with neither 40h nor
  80h set, `replace_equipment_stats` (rom 0x34437E) instead reads it as a small enum: 1/2/3 picks
  strength/agility/magic for a flat +10 bonus while worn, anything else raises both strength and agility (the
  same function also confirmed `power` doubles as the defense bonus an equipped item grants).
- `byte6` -- not identified. No decompiled routine reads item field index 6: not through
  `lookup_table_byte` (0 call sites with bx=6, out of 94 in `src/game/bank36_battle.cpp` /
  `bank36_misc.cpp` / `routines_bank36.cpp`, the only files that call it) and not through any of the other
  banks' direct reads of the item-pointer table `D_3941E0` (bank34_misc.cpp, bank34_yields.cpp,
  bank35_world.cpp). Values vary widely across items (0-160 in the extracted table), so this is live ROM
  data, not padding -- an unused or cut feature.
- `byte7` splits into `byte7_rest` (bits 0-6) and `ai_target_flag` (bit 7). Only bit 7 is ever read
  (`lookup_table_byte` bx=7, `src/game/bank36_battle.cpp` ~3065 and ~3245, the battle command-menu and
  target-menu input handlers): it's OR'd onto the halved `target` byte's bit 7 to bucket an equipped item's
  battle-command target kind. Both call sites mask the byte with `& 0x80` first, so bits 0-6
  (`byte7_rest`) are confirmed unread -- not identified.

**`tables/monsters` record** (meat_drop already known; then):
- `unknown_rank_bits` (byte0 bits 3-5) -- not identified. Loosely tracks each row's rank/HP within its
  `tables/monster_types` family (higher for tougher monsters within the same family), but no decompiled
  routine reads it back out.
- `esper_or_mecha` (byte0 bit 6) -- set only for the game's "esper"/robot classes: ids 158, 169-171, 175,
  176, 179, 180, 183, 184, 187, 188 and 197 (Psycho Soldier/サイコソルジャー, 28-gou/２８ごう, Mobile
  Infantry/きどうほへい, Mobile Machine/モビルマシーン, Esper Man/Gal x3, Suzaku/すざく in
  `tables/monster_types`). `initialize_creation_template` (`creation_template_core`, rom 0x34B21A) copies it
  into a new party member's +0Ah trait byte as 0x20 when bit 7 is clear. What that trait byte gates
  elsewhere isn't pinned down.
- `is_monster_row` (byte0 bit 7) -- set for every monster row (id 0-149) and clear for every human/mutant
  class row (id 150-199): confirmed directly against `tables/monsters`' own id range, not just inferred from
  the code. `initialize_creation_template` copies it into the new member's +0Ah trait byte as 0x40 (taking
  priority over `esper_or_mecha`); `history_select_member_core` and `menu_description_core`
  (`src/game/bank34_misc.cpp`) then skip, or disable the item/key-item display for, a member with that trait
  bit when auto-picking a leader or describing an inventory slot -- i.e. a monster-form party member has no
  normal inventory.
- `byte1` -- not identified. Copied into the battle actor's +1 byte for enemies only
  (`init_enemy_group_actors_core`, rom 0x3625A9); `init_battle_actors_core` (rom 0x3621EF) does *not* copy it
  for the four party actors. No decompiled routine reads battle actor +1 back. Values across the 200 rows
  are only 0, 1, 2, 4 or 8 -- shaped like a one-bit trait mask, but the bit meanings aren't identified.

**`tables/maps` record**: the last byte is `running_allowed`, already a named RAM variable
(0xD59C). `load_field_map_core` (rom 0x351334) copies it there only when `data_bank` is
nonzero; `prepare_player_scroll_core` (rom 0x350863) checks it to gate the run/dash button, when no field
script is active and `travel_mode` is 0.
