# Bugs in the original game

Behavior of the WSC original that looks unintended, found while reading the decompiled code. The port keeps
all of it: the game plays like the WSC by default. Fixing one is a deliberate decision, and a fix ships as a toggle
(bug fixes are toggles; some may default on). Each entry is also noted inline at its routine.

"Player effect" is read from the code and not yet checked in play; "?" means it isn't known whether it shows.

## Battle actions (`src/game/bank36_misc.cpp`)

| Routine | What the code does | Player effect | Status |
|---|---|---|---|
| `execute_strength_technique_action_core` | Uses the caster's **magic** (field 0Ah) and the target's raw defense, despite being the strength technique. | Strength techniques scale with magic. | kept |
| `execute_chainsaw_action_core` | The instant kill happens when adjusted strength is **below** the target's defense (comparison reversed). | Chainsaw kills more readily vs. tough targets: the famous chainsaw-kills-Creator trick. | **won't fix** ("that's probably what gets Creator") |
| `execute_elemental_magic_action_core` | Target magic is subtracted only for the "does it do damage" test; the damage dealt is the unreduced amount, doubled. | Elemental spells ignore the target's magic. | kept |
| `execute_innate_magic_damage_action_core` | The random spread is put in BH while the random helper reads AH (zero), and the target's magic is **added** to damage. | No damage spread; higher target magic means more damage. ? | kept |
| `execute_revenge_weapon_action_core` | The defense-reduction input saves a leftover register byte instead of the target's defense. | ? | kept |
| `execute_firearm_action_core` | The weakened-strength doubling is computed and never used; accuracy reloads raw strength. | Weakness doesn't affect firearms. ? | kept |
| `execute_magic_weapon_action_core` | On the extra-effect path, BX is overwritten with 5 before the accuracy addition, dropping the wear-plus-agility value. | Accuracy differs on that path. ? | kept |
| `execute_multi_hit_action_core` | The per-hit bonus saves target agility instead of the strength helper's result; zero attempts wrap to 256; damage of exactly 1 is discarded. | ? | kept |
| `execute_physical_status_action_core`, `execute_immunity_checked_status_action_core` | The random roll keeps the mask result as its lower bound; status bit 0 is tested twice, so petrification alone doesn't count as a success. | Petrify-only hits may report failure. ? | kept |
| `execute_self_destruct_action_core` | Compares the actor type with the loop countdown (27..1) instead of the item field just loaded. | ? | kept |
| `execute_gold_theft_action_core` | Writes at D86Bh (inside the actor records) instead of the gold at DD6Bh; the add loop counts on all of CX after loading only CL (CX = 0 runs 65,536 times). | Theft corrupts an actor record instead of taking gold. ? | kept |
| `execute_unanimated_fixed_damage_action_core` | Power + spread − defense can go negative and is passed on signed. | Fixed-damage attacks can heal. | kept |
| `apply_battle_status_effect_core` | Subtracts a full word while the high byte still holds an item pointer; the closing swap runs even when no swap happened. | ? | kept |
| `test_drain_hit_by_agility_core` | Attacker − target agility wraps as a byte instead of stopping at 0. | A slower attacker can get a high drain chance. ? | kept |
| several | Buffs wrap as bytes; drain heals by the overkill amount; the left pane's first row draws oddly. | ? | kept |

## Battle flow and growth (`src/game/bank36_battle.cpp`)

| Routine | What the code does | Player effect | Status |
|---|---|---|---|
| `resolve_status_hit_core` | Swaps actor and target on the immunity-fail path without an earlier swap; the resistance threshold subtracts a word whose high byte is an item pointer, so it goes negative. | Status resistance behaves oddly. ? | kept |
| `grow_mutants_after_battle_core` | Checks whether an ability may mutate by looking up item flags with its **uses count** as the item id. | Which mutant abilities can change is effectively random. ? | kept |
| `resolve_action_target_core` | Retargeting loops forever if no group has a live member and no slot holds FFh. | Probably unreachable (battle ends first). | kept |
| `priority_bubble_pass_core` | An inactive first actor doesn't advance the pointer, so the pass stalls on it. | Turn order may not be fully sorted. ? | kept |

## Items, equipment and shops (`src/game/bank34_misc.cpp`)

| Routine | What the code does | Player effect | Status |
|---|---|---|---|
| `initialize_monster_member_core` | Enters the ability-copy loop even when the template count is zero; its byte countdown wraps, copying 256 entries beyond the member record. | ? (requires a zero-ability template) | kept |
| `replace_equipment_core` | Saturates each equipment stat addition/subtraction independently at 0/255 instead of retaining the uncapped value. | Removing equipment after its bonus hit the cap can lower a stat below its pre-equip value. | kept |
| `draw_shop_sale_confirmation_core` | With zero remaining uses, `16*uses - 1` wraps to FFFFh, so the price loop adds 65,535 extra base prices before scaling. | ? (requires an occupied zero-use item) | kept |

## Field items and progression (`src/game/bank35_world.cpp`)

| Routine | What the code does | Player effect | Status |
|---|---|---|---|
| `use_field_cure_book_core` | Keeps only the low byte of `(power/4)*caster magic` as the random upper bound, then adds the roll, caster magic, target magic and power in AL before clearing AH. | Field magic healing wraps at 256; more magic or power can yield less healing. Healing staff/cure/healing spell entries share this rule. | kept |
| `use_field_revive_item_core` | Writes 1 only to the low HP byte, leaving the previous high byte intact. | Revived HP can exceed 1 if the dead member's old HP high byte was nonzero. Raise shares this rule. | kept |
| `use_third_tower_key_core` | Clears CF after the common lock helper, discarding its failure result; the first two key entries retain it. | ? (reports success even when no key was installed) | kept |
| `use_first_progression_item_core`, `use_second_progression_item_core` | Both combination entries clear CF even when the pair was unavailable. | ? (reports success after the unusable-item message) | kept |

## Shared palette drawing (`src/game/bank34.cpp`)

| Routine | What the code does | Player effect | Status |
|---|---|---|---|
| `palette_step_up_toward_core` | Blue/red stop at or above their targets; green stops only at equality, so a green nibble already above its target increments and wraps modulo 16. | Green can cycle through the wrong colors during an upward palette fade. | kept |
