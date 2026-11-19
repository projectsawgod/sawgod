# Code map

Where things live in SawGod, for someone who knows C++ and games but not the WonderSwan or this project's
history. It describes the code as it is for 1.0; it is updated as later releases restructure it.

## The big picture

The original game's code is decompiled into C++ that still runs on an emulated WonderSwan Color (`src/emu`): each
routine of the cartridge is a C++ function, checked against the original by differential tests. Around it, the port
adds its own screens, graphics modes, audio, saves and options. The ROM is an input: on first run the player's ROM
is extracted into resource archives (`wsc.sgz`, `gb.sgz`) that the game reads, and that mods override.

| Folder | What it holds |
| --- | --- |
| `src/emu/` | The emulated machines: the WSC (V30MZ CPU, memory, video, sound) and the Game Boy parts used for GB audio. Knows nothing about the game. |
| `src/engine/` | Host services that know nothing about the game: SDL window and input, config, archives (.sgz), mods, fonts, GPU passes for Enhanced, controller icons and the Options overlay. |
| `src/game/` | The game: the decompiled routines (`bank*.cpp`, `system.cpp`), the data and text it reads, and the port's screens and features that hook into it. |
| `src/app/` | The application: `main.cpp` (the frame loop and presentation), startup and launch arguments (`startup`, `launch_args`, `game_resources`, `save_setup`), first run, audio mixing (GB and Enhanced music), splash, input recordings (`input_movie`) and the screenshot runner (`shot_*`). |
| `src/tools/` | `sawgod-headless`, the window-less runner the tests drive (replays, captures). |
| `tools/` | Python and shell tools: the build's generators (shaders, Enhanced music), mod tools, packaging. |
| `tests/` | Unit tests (doctest and Python). |
| `android/` | Android gamepad app: SDLActivity, manifest, Gradle project and launcher icon. Build/storage notes: `docs/ANDROID.md`. |
| `assets/` | Port-made assets that ship (logo, world-card lettering, GB maps). Never ROM bytes. |
| `docs/` | Feature docs and this map. |

## How a frame runs

`app/main.cpp` owns the loop. Each frame it reads input, runs the emulated machine for one frame (the decompiled
game code runs as native C++; the CPU interpreter remains for tests and comparisons), mixes the frame's audio,
and presents the picture through the selected graphics mode: **WSC** (the original picture), **GB** (`GfxSwap`
redraws it with the Game Boy version's art) or **Enhanced** (GPU effects, Mode 7 world maps). The port's own screens
(`*_ui.cpp`) draw on top through `HostPicture` and `Chrome` (`ui_chrome.h`).

Game code that waits for the next frame runs on a fiber (`game/fiber.cpp`) and yields, so routines read like the
original's straight-line code.

## Where the game rules are

The decompiled code is still grouped by the cartridge's ROM banks (`bank34_*` menus and engine, `bank35_*` field and
world, `bank36_*` battle, `bank37` battle animations, `bank3c_title` title and intro, `bank3d_sound` sound driver).
Each routine is named and commented; `// rom: 0x...` gives its address. The rules a modder most often wants:

| Rule | Routine (`*_core`) | File |
| --- | --- | --- |
| Gold from battles, the 999999 cap | `award_battle_gold`, `add_gold_reward` | `battle_rewards.cpp` |
| Damage and random damage bonus | `apply_base_effect_damage`, `add_random_damage_bonus` | `bank36_misc.cpp` |
| Hit chance | `compute_agility_hit_threshold` | `bank36_misc.cpp` |
| Status effects in battle | `apply_battle_status_effect` | `bank36_misc.cpp` |
| Status recovery | `try_cure_status_40`, `try_cure_status_80` | `bank36_battle.cpp` |
| Escape | `attempt_battle_escape` | `bank36_misc.cpp` |
| Enemy AI | `choose_enemy_actions` | `bank36_battle.cpp` |
| Turn order | `battle_order_shuffle` | `bank36_battle.cpp` |
| Battle setup and turns | `setup_battle`, `execute_order_entry` | `bank36_battle.cpp` |
| Mutant growth after battle | `grow_mutants_after_battle` | `bank36_battle.cpp` |
| Meat and monster transformation | `lookup_meat_transformation` | `bank34_misc.cpp` |
| Random encounters | `roll_field_encounter` | `bank35_world.cpp` |
| Shops: buying and selling | `buy_shop_item_into_inventory`, `confirm_shop_sale` | `bank34_misc.cpp` |
| Healing outside battle | `add_field_healing_hp` | `bank35_world.cpp` |
| Field actions, map loading | `process_field_action`, `load_field_map` | `bank35_world.cpp` |
| Inventory and equipment menus (game side) | `run_inventory_tab`, `run_equipment_tab` | `bank34_yields.cpp` |
| New game and character creation | `new_game_setup` | `bank34_yields.cpp` |
| Random numbers | `random_range` | `bank34.cpp` |
| Sound requests | `sound_request` | `bank3d_sound.cpp` |

`battle_rewards.cpp` shows the style for rules made readable: the rule as plain typed C++ behind the original entry
point. Most other rules are still in the decompiler's register-level style (`m.ax()`, `m.rd16(...)`); restructuring
them is planned for later releases.

## Changing the game without code: mods

Most content changes need no C++: a mod is a `.sgz` (zip) or folder of override files (`docs/MODS.md`). Data tables
(items, shops, monsters, formations, encounter odds, meat), any text, field maps, graphics and the GB layer can all
be overridden. Table columns: `docs/DATA_TABLES.md`; text keys: `game/text.h`.

## Changing the game with code: hooks

Features attach to the game through hooks instead of editing its logic (`game/hooks.h`). With nothing subscribed,
every hook is a no-op and the game is exactly the WSC original.

- **Values** (`hooks::filter`): a number the game is about to use, which a subscriber may change. `BattleGold`,
  `BattleMeterChange` (damage and healing), `StatGain`, `FieldTeleport`, `TitleMenuOptions`, and key polls for
  several menus.
- **Decisions** (`hooks::should`, `VB::*`): a yes/no the game is about to make. `RandomEncounter`, `SpendUse`,
  `SpendHeart`, `InflictStatus`, `RunningAllowed`, `PlayIntro`.
- **Events** (`hooks::emit`): notifications, for UI, audio and presentation. Battle start/end, rounds, actions, HP
  changes, menus opening, sound and music requests, intro scenes.

Subscribe through the machine's session; the subscription lasts as long as its token:

```cpp
auto sub = game::Session::of(wsc).hooks().subscribe(game::hooks::Value::BattleGold,
    [](game::Machine &, int32_t gold, const game::hooks::Args &) { return gold * 2; });
```

`game/cheats.h` (Options > Modifiers: Encounters, Gold, God Mode, Unbreakable Gear...) is a complete worked example.

## The port's own screens and modes

| Area | Files |
| --- | --- |
| Menus, battle screen, dialogue, saves (the redesigned UI) | `game/*_ui.cpp`, drawn with `game/ui_chrome.h` |
| Text: keys, fitting, word wrap | `game/text.h` (host strings), `game/text_fit.cpp`, `game/text_wrap.cpp` |
| GB mode | `game/gfx_swap.cpp` (art), `game/gb_intro.cpp` (white-page opening and world cards, GB-map pans), `game/gb_battle_effects.cpp`, `app/gb_music.cpp` (GB music) |
| Enhanced mode | `engine/enhanced_graphics.cpp`, `engine/presentation_renderer.cpp`, `app/mode7_renderer.cpp`, `app/enhanced_music.cpp`; Enhanced always draws the HD menus (`engine/menu_fill.cpp`, `engine/hd_menus.cpp`) |
| Options, settings and controls | `engine/settings.cpp`, `engine/overlay.cpp`, `engine/input.cpp`, `engine/button_icons.cpp`; `docs/CONFIG.md` (L1/R1 step Swap in every game context) |
| Saves | `game/save_file.cpp`, `game/save_slots.cpp`; `docs/SAVES.md` |
| First run (ROM to archives) | `app/rom_screen.cpp`, `app/rom_screen_window.cpp`, `app/first_run.cpp`, `game/gb_extract.cpp`, `engine/sgz.cpp` |
| English title, opening, ending and world-card pictures | `game/title_menu_lettering.cpp`, `game/intro_lettering.cpp`, `game/ending_notice_lettering.cpp`, `game/world_card_lettering.cpp`, `app/world_card_overlay.cpp` |

## Checks

| Check | What it proves | How |
| --- | --- | --- |
| Unit tests | Engine, formats, UI rules | `ctest` in the build folder; setup and ROM gates in [BUILDING.md](BUILDING.md) |

Start here: [CONTRIBUTING.md](../CONTRIBUTING.md).
