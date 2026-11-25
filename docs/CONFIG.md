# Settings file (`sawgod.toml`)

The front end's settings: what the options menu (Esc / F1, or the gamepad's Select button) shows
and changes. Code: `src/engine/config.*` (the registry and file format), `src/engine/settings.*` (the
settings themselves), `src/engine/overlay.*` (the menu).

## Where it lives

`sawgod.toml` in the game's **data folder**, next to the battery save (format: [docs/SAVES.md](SAVES.md)), the
stored ROM copy (`rom/`), the resource archives (`wsc.sgz`, `gb.sgz`), `mods/` and the debug log's `logs/`
([DEBUG_LOG.md](DEBUG_LOG.md)) (`src/engine/data_dir.h`). The data folder is:

- `$SAWGOD_HOME`, when set;
- else the game's own folder, when it can write there (**portable**: a Windows zip, a Linux tarball);
- else the OS per-user folder -- a macOS `.app`, Flatpak, AppImage or Android install, and every development
  build (a CMake build tree):
  - Linux: `~/.local/share/sawgod/sawgod/`
  - Windows: `%APPDATA%\sawgod\sawgod\`
  - macOS: `~/Library/Application Support/sawgod/sawgod/`

## First run

Without `--rom` (or `$SAWGOD_ROM`) and without a stored copy, the game shows the **ROM screen** (Silver font,
English and Japanese): drop the ROM on the window, or Choose File; a supported ROM beside the game is offered
too. `.wsc`, `.zip` and `.7z` are accepted; only the exact supported dump (its SHA-1) is. The game copies it to
`rom/`, builds `wsc.sgz` and `gb.sgz` behind a progress bar (a few seconds), and goes straight to the title. A
missing, damaged or outdated archive is rebuilt the same way at the next start.

Archives identify extraction-code contents and, for GB resources, the names/contents of the authored
GB map inputs, rather than the application release/revision. An unrelated release or same-package
reinstall keeps them. The ROM is still SHA-1 checked on every launch. Disposable `.sgz.verified` receipts
avoid repeat CRC decompression after hashing the compressed archive contents; `map-banks.cache` holds the
ROM/map-header result of map compression, keyed by every ROM, header and parsed map byte plus extraction
code identity. Cache payload hashes detect damage. Deleting these caches is safe; they rebuild on demand.
`gb-field-maps.cache` holds decoded GB map pixels and lookups, keyed by each map’s layered PNG/lookup/cell/hidden inputs and the
extraction-code identity, with checksum/shape validation. Invalid resources are parsed again and reported.
On ext4, F2FS, Btrfs, XFS, tmpfs and overlayfs, `gb-inputs.cache` reuses the authored-input content hash
when the complete file list, size, nanosecond mtime/ctime, inode, device and code identity match. Ctime catches
same-size writes with restored mtime; atomic replacement changes inode/ctime. Other filesystems and Windows
hash the actual contents each launch. The first launch adopting these identities rebuilds old revision-keyed
archives once.
Config, translations and mods are read normally; map mods participate in the map-bank key after layering.
Startup stage durations and cache hit/miss appear in the [debug log](DEBUG_LOG.md#startup-timings).
`python3 tools/sgz.py unpack` opens an archive for reading; changes go in a mod (docs/MODS.md).

`sawgod --config FILE` uses another file. The menu writes the file when it closes (and at exit) if
something changed. A first exit also writes the file without menu changes when the app records the
development soundfont path; unchanged later runs leave the file alone.

## Rules

- **Game mechanics follow the WSC by default**, with deliberate departures for saves, UI and
  message speed. Settings include presentation, gameplay options and modifiers; cheats
  default off. A missing file, missing key or bad value uses the registered default.
- Command-line flags (`--scale`) override the file for one run and are not written back; changing
  that setting in the menu replaces the override and is saved.
- Keys the running build doesn't know are kept and written back unchanged, so a newer build's settings
  survive an older build saving the file. Values are kept; the comments are regenerated on every save.
  The file is saved only after a change (or written on first run): a run that changes nothing leaves it as is.

## Format

A small subset of TOML: `[section]` headers, `key = value` lines and `#` comments. A setting's name is
`section.key` (`video.pacing`). Values are `true`/`false`, integers (out-of-range values are clamped) or a
quoted choice name for enumerations. Example (all registered defaults; Enhanced defaults follow below):

```toml
[video]
window_width = 1120  # last windowed size (5x the 224x144 screen at first); not in the menu
window_height = 720
fullscreen = false
borderless = false
pacing = "smooth"    # "smooth" | "exact" | "lospec"
screen_mode = "ips"  # "off" | "lcd" | "ips" (default "ips")
renderer = "gpu"     # "gpu" | "legacy", startup-only; GPU failure falls back automatically
identity_shader = false # startup-only GPU pipeline diagnostic; no visual effect
graphics = "swap"   # "gb" | "wsc" | "enhanced" | "swap"

[audio]
music_mode = "swap" # same four levels; independent of Graphics
soundfont = "ref/soundfonts/GeneralUser-GS.sf2" # illustrative path; the app records its absolute development path
master_volume = 100  # 0-100, steps of 10
music_volume = 100
sfx_volume = 100
mute = false

[gameplay]
language = "en" # shown on System; config key unchanged
fast_forward = "4"
fast_forward_vfx = true
battle_speed = "1"
skip_intro = false
enemy_hp = true
battle_transparency = 30 # hidden; fixed at 30 percent
message_speed = 1
b_button = "run"         # Hold Button to Run
auto_target = true
meat_preview = true
confirm_esper_changes = true
y1 = "equip"
y2 = "item"
y3 = "menu"
y4 = "ability"

[modifiers]
encounters = "normal"
gold = "1"
damage = "1"           # party damage: "1" | "2" | "4"
stat_boost = 100         # 1x; 100, 200 or 400 percent
god_mode = false
one_hit_kills = false
unbreakable_gear = false
unlimited_revives = false

[input]
touch_controls = "auto" # Android or --touch-controls: "auto" (default), "on", "off"
swap_ab = false         # Swap A/B: Off by default, On exchanges gamepad A and B
pad_icons = "auto"       # Gamepad page, Icon Type: "auto" (default), "xbox", "ps", "switch"

[debug]              # only in builds with SAWGOD_DEBUG_MENU (on for development builds)
show_fps = false
```

| Setting | Meaning |
|---|---|
| `video.window_width`, `video.window_height` | The window's last windowed size; it starts at 1120x720 (5x), resizes freely and keeps the screen's shape. The picture fills it: nearest-neighbor at whole scales, sharp bilinear in between. `--scale N` sets the size for one run. |
| `video.fullscreen` | Desktop fullscreen with black bars around the picture (Alt+Enter toggles it). |
| `video.borderless` | Window without title bar or border. |
| `video.renderer` | `"gpu"` (default): SDL's GPU renderer through the existing drawing API, falling back to the legacy renderer when device, shader or startup capability checks fail. `"legacy"`: the original SDL backend selection, excluding GPU. Hidden from the menu; restart after changing. `--renderer gpu\|legacy` overrides it for one run. Which GPU: on Linux the GPU renderer asks SDL for the low-power device (the system's default, the one driving the display on a hybrid laptop), following the Linux convention that the player opts into another GPU; `DRI_PRIME`, `MESA_VK_DEVICE_SELECT` or `__NV_PRIME_RENDER_OFFLOAD` (`prime-run`) in the environment keep SDL's high-performance choice, and `SDL_RENDER_GPU_LOW_POWER` overrides both. Rendering on the discrete GPU and copying to the display GPU (PRIME) stalled a present every 0.41 s. Windows and macOS keep SDL's high-performance default (Windows' own convention; its per-app Graphics setting overrides it). Headless (`offscreen`) runs always take the low-power device. The chosen device is logged at start (`Presentation renderer: gpu (vulkan, <device>)`). |
| `video.identity_shader` | Off by default. Diagnostic pass-through fragment shader over the game's XRGB texture draws, cleared before IPS masks, options and badges. Hidden from the menu; restart after changing. `--identity-shader` enables it for one run. Failure falls back to legacy; the startup log reports the actual backend and shader state. |
| `video.pacing` | `smooth` (default): Enhanced submits every display refresh with vsync, interpolating completed field camera/actor positions one game step behind. WSC/GB treat Smooth as Exact. `exact`: submit the newest picture when a 75.47 Hz game step is due; 60 Hz skips some pictures, higher fixed refresh rates repeat them unevenly. `lospec`: 30 FPS presentation, including Options, with Smooth interpolation, a 3x (672×432) Enhanced scene cap and 16-voice Enhanced music. Host UI, text, minimap and touch controls retain output resolution. All modes keep every native tick and input service unchanged; only Lo-Spec changes audio (16 music voices). Saved values are retained. |
| `video.screen_mode` | Independent of `video.graphics`: every graphics level uses it as set, except the Enhanced title screen, which shows no screen effect once its logo takes over from the WSC logo, 1.5 seconds after the kanji fade in. `off`: plain square pixels; `lcd`: the stock WSC screen (reflective FSTN, no visible grid): lifted blacks, low contrast, pale desaturated color, a cool green-gray cast and smear in motion (src/engine/screen.h; tuned by eye against photos of unmodded units); `ips`: an IPS-modded WSC, each pixel as red, green and blue subpixel stripes over a dark bottom line (pixels under 3 output pixels get a grid). The IPS lines are one output pixel wide on the game pixels' exact edges (partial coverage at non-whole scales, so no moire), and their strength gives the same look at every size (the 6x look: the 1-pixel lines at 2/3 strength, scaled with the pixel size; the stripes at 0.4, so white keeps about 91%). At Enhanced, IPS uses half strength on every mask (lines and stripes, flat picture, UI, projected ground/actors and zoomed fields): each mask value m becomes 1 - 0.5 × (1 - m). WSC and GB keep full strength; Swap interpolates the strength continuously with the Enhanced weight. At Enhanced, `lcd` is a wash over the GPU picture: the stock screen's black and white per channel, without the desaturation, midtone lift or smear of the WSC/GB version. |
| `audio.*_volume`, `audio.mute` | Master, music and sound-effect (SFX) volume, and one mute. Music vs effects is by hardware channel: the sound driver plays effects on slots that take a channel over from the music (src/app/sound_mix.h), and each channel gets the volume of whichever is driving it. |
| (save slots) | Always 9 slots and the AUTO slot (not settings). Every new map the party reaches is saved into AUTO, the first entry of the save and load lists (docs/SAVES.md). |
| `gameplay.language` | Language: `"en"` (default: the English text resource given with `--english`, or `english.csv` in the per-user directory) or `"ja"` (the original). Without an English resource the game is in Japanese. Switches at once; both run on the native code (game/text.h). |
| `gameplay.fast_forward` | Fast-Forward: the speed while the Fast-Forward button (Tab / R2, rebindable) is held: `"2"`, `"3"` or `"4"` (default) times. The sound speeds up with it. A double arrow (no speed text) shows top right while it runs. Never changes how the game plays. |
| `gameplay.fast_forward_vfx` | Fast-Forward VFX (Gameplay, beside Fast-Forward): `true` (default) shows soft VHS tracking/chroma bleed in WSC/Enhanced and LCD persistence with a monochrome scan band in GB. Enters in 100 ms, clears within 120 ms after release. Live UI panes and host overlays stay clean. GPU: one final pass with a retained clean picture; software: the same tracking/persistence formula on composed output pixels while active. `false` keeps the ordinary picture. The double-arrow indicator remains in either case. |
| `gameplay.battle_speed` | Battle Speed: battles run at `"1"` (default), `"2"` or `"4"` times (from the game's battle start to its end; Fast-Forward held wins). Music and sound effects keep their normal tempo and pitch in every Music mode; only game steps accelerate. |
| `gameplay.skip_intro` | Skip Intro: straight to the title menu (no SQUARESOFT logo, story or title reveal); also after the menu's timeout. Off by default. |
| `gameplay.battle_transparency` | Battle plate transparency is hidden and fixed at its default 30%; any other value loads as 30. |
| `gameplay.enemy_hp` | Enemy HP: in battle, "HP/max" of each enemy group's first living member right above its picture, as a red HP bar like the party's, with its compact numerals (no padding zeros; at least 32 px, wider for long numbers; game/info.h). Drawn over the picture only: the game is unchanged. On by default. |
| `gameplay.message_speed`, `gameplay.b_button`, `gameplay.auto_target`, `gameplay.meat_preview`, `gameplay.y1`-`y4` | **The game's own Config tab, moved here** (game/game_config.h): Message Speed choices, slow to fast: 8, 5, 3, 2, 1 (default), Instant (stored as 11, native byte 10); other values load as the nearest choice; Hold Button to (the game's B, remappable) `"walk"` / `"run"` (default, as the game's); Auto-Target and Monster Meat Preview (default on, as the game's); what Y1-Y4 open in the field (`item`, `ability`, `equip`, `order`, `gallery`, `save`, `menu`, `none`; default equip, item, menu, ability). The app writes them into the game's own bytes (0xD0E6-0xD0EB) before every frame, so the game reads them as always and they are saved with each game; dash is always on; the party menu has no Config tab (Save moves up). |
| `gameplay.confirm_esper_changes` | Confirm Esper Changes (Gameplay, below Monster Meat Preview): on by default. Label and help are keyed Options text (`ui_options_confirm_esper_changes`, `ui_options_ask_before_replacing_an_esper_ability`). Only replacing an occupied Esper ability slot asks for Yes/No; filling an empty slot never asks. The proposed ability has already been rolled. The host pauses the machine, including RNG, keypad, raster and audio clocks, until the choice is made. Yes applies both ability and uses exactly as before and shows the normal replacement announcement. No preserves both bytes and counts as no growth, without rerolling; the existing announcement presents “No change.” while preserving its native scratch writes and timing. B also declines. Other growth outcomes and later rolls are unchanged. |
| `modifiers.encounters` | Cheats page (Boosts and Cheats), boosts first. Encounters: `"normal"` (default), `"half"` (every second random battle skipped) or `"off"` (no random battles; scripted battles still happen) (game/cheats.h). The Encounters button (C / left stick click, L3) switches Off and back; "Encounters Off" shows top right while they are off, "Encounters On" for 2 seconds after. In Enhanced its width and height are halved; its translucent fill shows the current rendered field, including the Enhanced camera and effects. |
| `modifiers.gold` | Gold Boost: each enemy's gold reward times `"1"` (default), `"2"` or `"4"`; rounded half up; the game's 999,999 cap stays. |
| `modifiers.damage` | Damage Boost: party damage dealt to enemies by attacks, spells and items times `"1"` (default), `"2"` or `"4"`, applied once at the final HP change and capped at 9,999. Healing, misses, damage to the party and enemy damage to enemies are unchanged. |
| `modifiers.stat_boost` | Stat Gain Boost: every stat gain (a mutant's growth after a battle; a human's strength, agility and HP items) times 1x, 2x or 4x (stored as 100, 200 or 400 percent). Other percentages load as the nearest choice; a tie keeps the lower multiplier. Rounded half up; the game's caps (99, max HP 999) still apply, and a multiplied mutant gain that would pass 99 is capped instead of dropped. Default 100 (1.00x). |
| `modifiers.god_mode` | God Mode: the party takes no damage and no negative effects in battle (stone, death, poison and every other status: "no effect") and no damage in the field (damage floors such as the floor 4 spike room, scripted HP losses); heals still work. Off by default, like every cheat (game/cheats.h). |
| `modifiers.one_hit_kills` | One-Hit Kills: any damage to an enemy takes all its HP (a miss is still a miss). Off by default. |
| `modifiers.unbreakable_gear` | Unbreakable Gear: the party's weapons, items and abilities never spend a use, in battle or from the field menu. Off by default. |
| `modifiers.unlimited_revives` | Unlimited Revives: reviving a member never costs a heart (the paid revive's gold is still paid). Off by default. |
| (Cheats) Teleport | Not a setting: a menu row picking a tower floor from 1 to the highest reached (the save's "N F reached"; Left/Right, starting at the current floor; "-" before a game is loaded). A closes the menu, and the next time the party stands still in the field it goes there as the Door does (warp effect, the floor's entry; on foot only). |
| (menu) | Left/Right on an option picks a new value that only takes effect when A is pressed (it blinks, and the help line says "Press A to apply, B to cancel"); B or moving off the option drops it. Bars (including Bloom and Message Speed) apply as they move. While the menu is open the paused frame, blurred and slightly darkened, fills the window behind it (only the picture's area in fullscreen), and the menu windows follow the current Graphics style at 90% opacity (GB paper, WSC texture, or the Enhanced gradient) so it shows through. Always on; not a setting. |
| `debug.show_fps` | Game frames per second, pictures shown per second and the display's refresh rate, top left. |

## Controls (`[keyboard]`, `[gamepad]`)

Each WSC button, and Options (the button that opens this menu), has two keyboard and two gamepad slots,
rebound in the menu's Keyboard and Gamepad pages (Left/Right pick a slot, A waits 5 seconds for the key or
button (a few very long key names show short: 1000Sep, PrevTrk, NextTrk, MediaFF), whatever opens Options cancels, Delete or the pad's north button clears the slot; "Reset
Bindings", set apart at the bottom, restores that page). An input already on another button **swaps**: that
button gets the slot's old binding, and the help line says "Swapped with B"; no input is ever on two buttons.
**Options is rebindable like everything else** (Esc and F1; the pad's Select button; buttons and keys
only, no stick or trigger). Gamepad edits and conflict swaps are validated together: they cannot remove
the last usable button for Accept, Cancel, or Open Options ("Keep buttons for Accept, Cancel and Options").
Keyboard edits retain the last Options key ("Options must keep one key"); an empty file entry restores it.
Options gamepad Accept/Cancel follow the effective A/B bindings, including custom second slots. Active
Accept/Cancel take priority over north's Clear and the shoulders' page shortcuts, consuming a press once.
D-pad and left-stick navigation stay available to reach Reset Bindings. Legacy empty, axis-only,
Guide-only, or directional-only menu actions keep their stored gameplay slots; Options recovers each missing
action with a distinct free menu button, in A/B/Options order, choosing south, east, back, start, west,
north, L1, then R1. Its hints show those actual controls in the selected icon family. Recovery never steals
another working menu action and never rewrites saved bindings. The menu's keyboard navigation stays fixed. System > Reset to Default Settings
resets what the menu shows; it leaves the bindings, the renderer and the remembered window size alone.
One set of gamepad bindings serves every pad.

Defaults: arrows / D-pad + left stick = X pad; WASD / right stick = Y pad. **L1/R1 step Swap down/up
in every game context, native menus included**. Y2/Y4 use D/A and right-stick right/left.
**R2 is held Fast-Forward in every game context, menus included**; **R3 (and M) cycle the world-map minimap** (below).

X / south = A, Z / east = B; Enter / Start or north = Start; Esc + F1 / Select = Options; Tab / R2 = Fast-Forward;
C / L3 = Encounters; M / R3 = Minimap. West and L2 are unbound by default. **G / L1 = Mode Down; H / R1 = Mode Up** (one fidelity level down / up). The step buttons act
outside Options, including inside native menus. Both move only the tracks set to Swap, without wrapping or a badge. Keyboard and Gamepad
rebinding pages list both actions. Gamepads use physical button positions and show the pad's own names.

The file lists only the WSC buttons whose bindings differ from the defaults (code: `InputMap::load/store`,
src/engine/input.cpp); a button not listed keeps its defaults, `[]` unbinds it. An empty string reserves an empty
slot, so `["", "Z"]` keeps Z in slot 2 after restart.

Example bindings:

```toml
[keyboard]
a = ["Z"]                  # SDL key names: "Up", "Return", "Space", "Left Shift", "Keypad 6"...
b = ["X"]

[gamepad]
start = ["start", "rt"]    # south east west north start back lb rb lstick rstick dpad_up dpad_down
y1 = []                    # dpad_left dpad_right lstick_up/_down/_left/_right rstick_... lt rt
```

Keys are `x1`-`x4` (X pad up, right, down, left), `y1`-`y4`, `a`, `b`, `start`, `options` (Options always keeps at least one binding per device). Unknown names are reported
at start-up and skipped; an input listed for two buttons stays on the later one.

**Button icons** (assets/ui/button_icons.png + .csv, engine/button_icons.h): the binding
pages show each binding as an icon: keys as keycaps sized to their label, pad buttons, triggers and sticks by
position in the Icon Type's family; the WSC column shows the WSC's own buttons as badges. Bindings without an
icon (guide, paddles, touchpad, the extra buttons, keys outside the 3x5 lettering) show their names. Text
writes `{A}`, `{B}`, `{X1}`...`{Y4}`, `{Start}` for "the button the player presses for that WSC button" (the
binding on the device they last used) and `{icon:NAME}` for one icon by its index name (`{icon:key:DEL}`:
a keycap); the options menu's help line shows its own fixed accept/back/clear buttons that way.

| Setting | Meaning |
|---|---|
| `input.touch_controls` | Touch Controls: Auto (`"auto"`, default) shows the overlay without a real controller and hides it while one is connected. On (`"on"`) always shows it alongside controllers; Off (`"off"`) hides it. Saved; controller connections never change this choice. Android or desktop `--touch-controls` only. Real pads and keyboard always work; Android Back opens/closes Options even under Off. |
| `input.swap_ab` | Swap A/B, above the bindings of the Gamepad page: Off (`false`, default) or On (`true`). Exchanges the configured gamepad A/B actions in gameplay and Options immediately. Confirming a change returns focus to the Gamepad category with Options still open; canceling an unconfirmed choice leaves the setting unchanged. Stored custom slots are preserved, so toggling twice restores them; the binding rows show the effective actions and edits target their underlying slots. A/B precede the X/Y pads on both binding pages. The press that changes the setting or completes capture is ignored until released; the next press uses the new mapping. Keyboard bindings are unchanged. |
| `input.pad_icons` | Icon Type, below Swap A/B on the Gamepad page: which family's icons the pad buttons show, by position. Auto (`"auto"`, default): the connected pad's family by its SDL gamepad type (PlayStation: PS, Switch: Switch, anything else or none: Xbox). Xbox (`"xbox"`, Xbox: A/B/X/Y, LB/RB, LT/RT), PS (`"ps"`, PlayStation: cross/circle/square/triangle, L1/R1, L2/R2, L3/R3), Switch (`"switch"`, Nintendo Switch: B/A/Y/X by position, L/R, ZL/ZR, +/-); named by console. Changing it (A, or a click) refreshes the page immediately. In the WSC and Enhanced styles each type has its controller's colors: Xbox near-black buttons with the face letters in Xbox green/red/blue/yellow, PS light buttons with the face shapes in PlayStation's colors, Switch dark gray buttons with light letters; shoulders and triggers are blocks rounded on their own side's top corner, sticks are discs, their direction arcs orange against the disc (gray in GB); under a dark button the shadow is a lighter gray. GB stays black. Headless `--button-icons key|xbox|ps|switch` selects the same families for hint tokens. |

## Adding a setting

Register it in `Settings::register_all` with a name, category (the menu page), label, a one-line
description and its default (the WSC behavior). The menu lists it automatically. **The description must
fit the menu's help line: at most 46 characters** (`sawgod::kHelpWidth`); the unit tests fail otherwise.
Enumerations can carry a per-choice description (`choice_help`), shown while that choice is selected.

## Fidelity settings

`video.graphics` (**Graphics**, Video) and `audio.music_mode` (**Music**, Audio) independently select
`"gb"`, `"wsc"`, `"enhanced"` or `"swap"`. Both default to Swap, starting Enhanced every session.
GB < WSC < Enhanced: G/L1 steps down, H/R1 steps up, stopping at the ends. A fixed setting locks its track;
any Swap track still steps. Picture and music fades each last 0.5 seconds. Entering Enhanced music first
prepares its playback queue as described below; its fade starts after that preparation. Interrupting
an active fade starts the next one at its current weights. There is no fidelity badge.

Enhanced music uses live FluidSynth with GeneralUser GS for the twenty-two locked castings in
`tools/enhanced_music.json`: songs 135, 138, 139, 140, 141, 142, 143, 144, 145, 146–151, 154 and 163,
plus concurrent jingles 136, 137, 152, 153 and 178. 141: the revised mix with only the Flute at 2× amplitude (+6.02 dB),
including its reverb, before full-mix RMS matching. 143: isolated voices,
staccato String Ensemble 1 at 2× amplitude, gate 35%, CC73/CC72 both zero, and fingered bass
one octave down. Its fractional note-offs use the offline MIDI's ten ticks per driver frame;
the current duration and sequencer phase predict the gate without extra playback delay.
140: a free arrangement with both upper melodies on String Ensemble 1, the intro high
figure on Celesta, and the high arpeggio on Orchestral Harp; isolated voice synthesis preserves
the stem sum and gain 3.836707478 through the finite release/reverb tail.
145 (regular battle): Violin, Muted Trumpet with its absolute gain ramp,
and fingered bass one octave down, plus the standard drum kit. 143 is the boss battle and uses the power kit.
142 (last battle): intro choir/organ octaves,
String Ensemble 1, Feedback Guitar at pitch over Distortion Guitar an octave down, lowered opening trumpet,
choir/Church Organ responses an octave down, boosted fingered-bass pair and accompaniment duck, and trombone.
Calls use guitar +2 dB, trumpet +0 dB, strings +0.5 dB; the section lift is +1.5 dB. The standard kit
and timpani (GM program 47 on a separate melodic channel) use a fixed PCM gain;
first-part drums (bars 7–22) add +3 dB with 20 ms ramps and the bar 7 landing hit.
Layer/program/transpose/volume/seat sections change at note onset, keeping ties and release tails.
Independent casting `envelopes` multiply PCM, including reverb; tick anchors follow the driver and
can repeat at `cast_loop_tick` / `cast_loop_ticks`. Casting and percussion serve both pipelines.

Authored drums live in `tools/audio_enhanced_drums/142.tab`, `143.tab` and `145.tab`. Editing a tab and
rebuilding regenerates the live event table. `bar` lines number measures; each instrument line has
16 cells (sixteenths) or 12 (eighth-note triplets). `.` rests, `x`/`X`/`g` give normal/accent/ghost hits,
and digits 1–8 give graduated velocities (clamped to MIDI 127). `# Levels:` overrides per-instrument
velocities and `# Notes:` selects kit notes. `T` requires one pitch name per hit; `Q` is pedal hi-hat.
143's bars 12, 16 and 20 have three beats. Kit, loop ticks, fixed PCM gain, first-part gain and ramps
are stored in the song's casting `drums` entry; no runtime RMS measurement occurs. The −6 dB reference level
is pinned, including the final shared mix gain for 143/145. Drum events use the same driver clock
and playback queue as the voices, carry tails across loops, follow Music volume and its fidelity fades,
and sound only in Enhanced. GB/WSC synthesis and SFX routing retain their behavior.

The WSC driver remains the clock: both versions run even when inaudible,
and the 0.5-second fidelity fade changes their weights.
Every Enhanced voice uses an orchestra seat plus **100% of the WSC movement**, including locked songs. Optional casting `seat` is percent, negative left, in −100..100; absent values use `tools/audio_seats.py`, which also feeds the generated live C++ castings. Layers default to their own effective instrument’s seat; an explicit voice/section seat is inherited unless a layer overrides it. `CC10 = clamp(round(original_WSC_CC10 + 63 × seat / 100), 0, 127)`, with ties to even. The original WSC CC10 is rounded first. Pan changes occur at existing note onsets; ties keep their current pan. Instruments, notes, levels, reverb and SoundFont stereo spread remain unchanged. Authored kits and timpani remain centered at normal SoundFont width, without orchestra-seat offsets.

| Seat (%) | Instrument families |
| ---: | --- |
| -30 | Violin; Tremolo strings; String Ensemble 1; Synth strings; Fiddle |
| -25 | Piano, celesta, harpsichord, mallets and bells; Orchestral harp; Kalimba; Pitched bells and mallets |
| -20 | French horn |
| -12 | Acoustic and electric guitars; Plucked world instruments |
| -10 | Oboe and English horn; Piccolo, flutes, pipes and ocarina; Bagpipe; Shanai |
| +0 | Organs, accordion and harmonica; Acoustic and electric basses; Choir, voice and orchestra hit; Synth leads, pads and effects; Percussion and sound effects |
| +10 | Viola; Timpani; String Ensemble 2; Saxophones; Bassoon and clarinet |
| +15 | Trumpet; Muted trumpet |
| +25 | Cello; Pizzicato strings (cello section by default); Trombone and tuba; Brass section and synth brass |
| +30 | Contrabass |

The register exceptions are explicit: 138 middle strings +10 and plucked bass +30; 140 middle strings +10; 143 lower strings +15. Other pizzicato defaults to the cello section (+25); other String Ensemble 1 defaults left (−30). A second guitar player can override the default −12 to +12. The additional GM families complete the table for further castings.

| Locked song | Voice seats: WSC slot/instrument = percent |
| --- | --- |
| 135 | 4/13 = -10; 5/5 = -25; 6/4 = -30; 6/5 = +10; 7/14 = -10 |
| 136 | 0/4 = +10; 1/5 = -10; 2/4 = -20 |
| 137 | 0/4 = -20; 1/4 = -25; 2/4 = +10 |
| 138 | 4/7 = +15; 5/3 = +10; 6/5 = +30; 7/5 = +10; 7/11 = -10 |
| 139 | 4/3 = -30; 5/5 = -10; 6/0 = +0; 7/3 = +10 |
| 140 | 4/5 = -30; 4/4 = -30; 4/14 = -25; 5/5 = +25; 5/9 = +25; 6/5 = +10; 6/4 = -20; 6/13 = -10; 7/13 = +10; 7/5 = -10; 7/14 = -25 |
| 141 | 4/3 = -30; 4/4 = +25; 5/5 = +30; 6/3 = -30; 6/13 = -10; 7/13 = +25 |
| 142 | Choir/organ = 0; guitar = -12; trumpet = +15; bass = 0; 7/5 inner strings = +10; trombone = +25; 4/5 later strings = -30 |
| 143 | 4/5 = +15; 5/5 = -30; 6/5 = +0; 7/3 = +10; 7/4 = +0; 7/5 = +10 |
| 144 | 4/4 = +10; 5/5 = -10; 6/3 = +25; 7/5 = -25 |
| 145 | 4/13 = -30; 4/5 = -30; 5/13 = -20; 5/4 = +15; 6/3 = +0; 7/4 = -20; 7/5 = +10 |
| 146 | 4/5 = +0; 5/4 = -10; 6/13 = -25; 7/13 = -10 |
| 147 | 4/4 = -20; 4/5 = +25; 5/4 = +10; 6/5 = +0; 7/5 = -10 |
| 148 | 4/5 = -25; 5/5 = +0; 6/13 = -10; 6/14 = -10; 7/5 = -10; 7/11 = -10 |
| 149 | 4/4 = +15; 5/5 = +15; 6/1 = +0; 7/4 = -25 |
| 150 | 4/5 = +25; 5/5 = -10; 6/4 = -25; 7/13 = +10; 7/14 = -10 |
| 151 | 4/13 = -10; 5/4 = +10; 5/5 = +25; 6/6 = -25; 6/14 = -10; 7/6 = -30; 7/14 = -10 |
| 152 | 0/5 = +15; 1/5 = +15; 2/4 = -20 |
| 153 | 0/4 = +0; 1/4 = +0 |
| 154 | 4/5 = +25; 5/4 = +10; 6/3 = +25; 7/13 = -10 |
| 163 | 4/5 = +0; 5/4 = -10; 6/13 = -25; 7/13 = -10 |
| 178 | 0/5 = +15; 1/5 = +15; 2/4 = -20 |

The cue stream preserves the continuing field song and treats its owned effect slots as Music volume,
including when the field song uses WSC. Ordinary SFX can interrupt those slots; completed cues return
the hardware to the field song without restarting it. Request 178 retains its own locked casting;
immediate effect 160 selects that alias. Finite songs keep their natural synth release and reverb. Cue 153 ends naturally; song 149 uses
the music slots and repeats its observed 512-tick driver phrase until the game replaces it.
Pause-menu music and Fast-Forward use the same mixer. Rendering and soundfont loading run on one
worker. When Music is configured as **Swap or Enhanced**, all three music arrangements,
including fallback songs, effect-slot jingles, authored percussion and layered voices, retain the
same eight driver frames (106 ms at normal speed). Their 0.5-second fidelity crossfade changes
weights only: it never stretches, resamples, bends pitch, or changes the music cursor. Music may
lag the picture and ordinary effects by those 106 ms. WSC and matched GB **sound effects bypass
the music lookahead** and retain their existing hardware ownership and native effect clocks.
The two chips provide separate music/effect PCM with independent DC filters; only music enters
the synth's lookahead. One current effect frame joins one delayed music frame before master gain
and the existing Fast-Forward grain processing.

Starting with Music locked to GB or WSC adds **zero lookahead delay**. A runtime change to Swap or
Enhanced primes the eight-frame music queue once (106 ms of silent music; SFX continue). Locking
an already delayed song keeps its cursor until the next background-song request or reset after
its Enhanced fade has finished, then removes the delay at that song replacement. Effect-slot
jingles do not trigger this change. This preserves the current song without skipping or replaying
106 ms of it when a lock changes. Fidelity stepping within Swap never primes or drains the queue.
Missing FluidSynth or a missing soundfont adds no lookahead, even when Enhanced is selected. The
SDL output buffer and ordinary worker scheduling are separate from this added music lookahead.
The inaudible synth still observes eight future frames for locked velocities and articulation.
Options continues normal-speed music ticks; Fast-Forward changes all music and SFX through the
existing pitch-preserving grain processor after the two timelines are mixed.

`audio.soundfont` is a file-only string setting; changing it requires restarting the app. The development
default is the build's source checkout `ref/soundfonts/GeneralUser-GS.sf2` (an absolute path written into
new configs; `python3 tools/release/fetch_soundfont.py` downloads it). Point it at any other copy if needed. Missing
FluidSynth or a missing/unreadable soundfont keeps WSC music and logs once, with no dialog. The app loads
FluidSynth dynamically (`libfluidsynth.so.3`, `libfluidsynth.3.dylib`, or the FluidSynth Windows DLL); it does
not statically link it. Release packages ship the library, soundfont and their license materials;
see [RELEASE.md](RELEASE.md). `SAWGOD_FRAME_LOG` adds `audio_synth_ms` (last completed synth frame, on the worker) and
`audio_dropped` (bounded-queue overflow count). GB music and matched effects keep the existing driver. GB graphics receive no Enhanced effects. WSC/Enhanced fades the complete scene effect result over
the ordinary WSC picture; GB/WSC fades the two live pictures. Protected UI retains its ordinary pixels.

`--enhanced-graphics` is a session-only alias selecting Graphics Enhanced.

World-map dash uses the existing B-button rule on maps 88, 102, 122 and 136 and their later-story copies 199 (of 122) and 24 (of 136), on foot and on the World 2 floating island (travel mode 1, which also
follows the map's own gate elsewhere), outside scripts; the Glider and the Bike keep their speed.
This is a deliberate departure from the WSC; ordinary field running gates are unchanged. Headless
runs and app input movies disable the host GameConfig rule, preserving their original equivalence goldens.

## Enhanced tuning (WSC scene only)

Enhanced is always available as a fidelity level. Snap, shadows and grade remain hidden from
Options. Lighting FX, Water FX and Bloom FX are On/Off rows under Video’s Enhanced header. Set hidden tuning
in config or use `--no-snap-to-grid` for this run. Defaults:

```toml
[enhanced]
battle_lighting = true # Battle Lighting: stage/corner dimming and pulsing target light
snap_to_grid = true
contact_shadows = true
beam_style = 0   # hidden: 0 cone (default), 1 parallel shaft, 2 cone with dust
warm_grade = 0    # 0–300 percent of the warm look (off by default)
bloom = 40       # Bloom FX: On = 40 (default), Off = 0; any nonzero value is On
lights = 100      # Lighting FX: On = 100 (default), Off = 0; any nonzero value is On
water = 100       # Water FX: On = 100 (default), Off = 0; any nonzero value is On
field_zoom = 125      # Field Zoom: 100, 125 or 150 percent pull-out; whole output pixels
mode7 = true          # Options → Video → Enhanced → Mode 7 World Map: the world map's tilted camera only
depth_of_field = true # Options → Video → Enhanced → Tilt-Shift: softened edges on every map, the same at the same distance in from any edge
mode7_focus = 30      # hidden tuning of Depth of Field: 0–100 percent, its strength
mode7_focus_size = 50 # hidden tuning of Depth of Field: 40–100 percent, size of the sharp middle (the soft band along the edges is (92 − 90 × size) × 32/47 native px deep: 32 px, two tiles, at the default)
minimap = true        # hidden; the Minimap button cycles Off/Small/Large
minimap_size = "small" # "small" or "large": the size it shows at (hidden; the Minimap button sets it)
```

**World-map minimap** (src/app/minimap.h). Only on the four world maps (88, 102, 122,
136), while the party walks the world field in Enhanced graphics with the Mode 7 world camera: hidden in
battles, menus (native and Options), message boxes, map transitions and fades, while Fast-Forward is held, and in
GB/WSC graphics. It fades in and out over 0.2 s and follows the Enhanced weight through a fidelity switch. It
shows the whole 64×64-tile world, fully revealed (not the world atlas's 8-tile authored border), 2x2 averaged colors per
tile from the world camera's own mip pyramid, its colors at 80% brightness, 90% opaque, no frame or border.
A single game-sized pixel, no outline, marks the party's tile: a muted 80% white that dips smoothly to 30% and
back (0.7 s) every 2.5 s, like a breath. The Ocean world (102) drops its top and bottom tile rows and the Sky world
(122) two rows and one column each side, edge tiles that read as a border. It sits in the game picture's bottom-right corner, inset 4 native pixels, drawn at window
resolution over every scene effect (bloom, lights, focus blur, IPS), under the badges: Small about 25% and
Large 1.5x Small, rounded so each tile is a whole number of window pixels (4 and 6
at 1728×1111). The **Minimap** button (M / R3, rebindable; `minimap` in `[keyboard]`/`[gamepad]`) cycles
Off → Small → Large → Off; it is read only on the world-map field. Options' On shows the last size again
(Small the first time); `minimap` and `minimap_size` persist.

World camera geometry stays fixed (`game::Mode7Look`): 16-degree tilt, 24-degree lens,
1.50× distance. Fields default to a requested 1.25× pull-out (Field Zoom).

Enhanced always uses the HD menus (there is no switch): output-resolution treatments. Window fill: host windows use a smooth,
output-resolution vertical gradient from the palette's brightest fill color to 90% of its darkest.
Joined tabs and speaker windows share one span. WSC and GB always keep their original fills. Concave tab connectors also render their rounded frame stroke at output resolution. Native pictures remain unchanged.
Text: glyphs keep their original pixels, layout and metrics, with a soft shadow. The glyph mask is offset
0.75 game px right/down, Gaussian blurred (sigma 0.45 game px), colored (17,17,17), and given
alpha min(1, coverage × 1.125). Icons, cursors and straight frame sections retain their original treatment. WSC and GB keep
their original text. Text deliberately drawn without a shadow (dense Bestiary labels and
captions) stays shadowless.
HP bars: the original fill and numerals stay fixed, without a border. The bar casts an outside-only soft shadow: 0.5 game-px right/down offset, Gaussian sigma 0.35 px,
(17,17,17), peak alpha 0.85. These edges fade with Enhanced × (1 − GB). WSC always uses the
1 px hard shadow (font shadow ink, down-right).
GB retains its numbers alone, without bars or edges.

`enhanced.field_zoom` is **Field Zoom** (100% / 125% / 150%, default 125%): town and dungeon camera
distance, independent of `enhanced.mode7`. At 100% the field uses its ordinary framing. The opening's demo (the
tower climb before a game starts) keeps its ordinary framing at every level.
`enhanced.mode7` is **Mode 7 World Map** (On/Off), controlling only the world map tilt. Video has Graphics, Fullscreen,
Borderless, Performance (Smooth / Exact / Lo-Spec), Screen Mode, then an Enhanced header and Mode 7 World Map,
Field Zoom, Tilt-Shift, HD Battles, Bloom FX, Lighting FX and Water FX. Its 13 rows fit one screen.
Enhanced rows dim and cannot be selected in GB/WSC; they remain available in Enhanced/Swap.
Bloom FX On is 40% strength; Lighting FX and Water FX On are 100%.
All three effects accept 0 as Off and every nonzero percentage as On.
`--shot-camera TILT,LENS,DISTANCE,ZOOM` (degrees, degrees,
multipliers) selects another camera for one accelerated screenshot run only.

Contact shadows follow the lowest opaque row of the drawn party/NPC art, including walking poses and
transparent bottom padding, rather than the 16×16 cell bottom. Projected shadows share the upright card’s
output-pixel anchor. Party/NPC ellipses have radii 6.6×2.2 native pixels; battle ellipses keep their
portrait-cell anchors.

All performance settings use 224×144 light/shadow fields, at most 32 visible field sources and
eight spell groups. Unchanged light planes reuse their resident texture. Bloom extracts and blurs
at 56×36 (formerly 112×72), keeping the existing blur reach and full battle strength. These limits
are fixed and add no options; Lo-Spec still controls its existing scene cap and pacing.

The current effects include actor/enemy contact shadows, warm grading, bloom, tagged lights,
tagged water and the native fire-landing light. Warm grade defaults off; map bloom
(field maps and world maps) uses 25% of the setting and battle bloom 100%. Field dialogue keeps effects while map, scroll
and screen bases remain unchanged; native fire light reaches enemies/background/floor, never UI
or the effect art. Bloom extraction and both Gaussian axes use fixed 672×432 GPU targets (22 bilinear taps per axis).
Projected ground, actor edges, ownership, water metadata and the final scene composite stay at output
resolution. Flat scenes remain at 3× and reuse unchanged uploads across refreshes; UI and IPS are
composited at output resolution. Snapped flat effects are quantized to the 224×144 game grid. UI is restored from
the ordinary picture, including transparent windows, text, cursors, host pictures and battle panels.
The WSC scene is enhanced before crossfading with the unchanged GB picture. Options and badges stay
outside the effects. No silhouette shadows.

`enhanced.battle_lighting` (Options → Video → Enhanced → HD Battles, default On) adds
battlefield lighting at output resolution, fading with the Enhanced weight. Only background rows 16–79
receive corner dimming (12%, without blur) and stage dimming (18% outside Gaussian
34×40 px cones at visible enemy picture centers). During enemy-target selection the selected group's gain
pulses from 1 to 2.2 over 1.2 s and its own pixels lift by up to 18% with a slight warm addition; the
selected group also gets an outline: a 1 native px warm near-white (255,232,200) ring on
the background cells touching its picture, with a 0.5 px glow, pulsing between 70% and 100%; gaps with the
picture within two cells on both sides (between legs, under arms) are not outline, and UI covers it. Moving
selection crossfades both in 0.25 s; leaving selection clears them. There are no floor pools or target
room light. UI, HP bars and effect art stay protected; enemies receive only the target lift. WSC/GB stay unchanged. This is part of the existing final-scene-ui GPU pass (no new
render targets or passes); `SAWGOD_GPU_PASS_TIMING=1` logs it as `battle-lighting-final` with the other
passes (includes the ordinary final composite). Additive light follows scene exposure through black fades.
The hovered monster plate uses 40% opaque fill in Enhanced, in Enhanced; its frame
and text stay opaque. WSC keeps the Battle Transparency setting; GB keeps its opaque chrome.

**Battle animations.** Fire, ice, thunder, poison, sleep and death treatments and the
casting/delivery glow are all **on** in Enhanced, through `SceneObserver`; no Options or settings are
involved. For a before/after capture, setting one of `SAWGOD_BATTLE_ICE`, `SAWGOD_BATTLE_THUNDER`,
`SAWGOD_BATTLE_POISON`, `SAWGOD_BATTLE_SLEEP`, `SAWGOD_BATTLE_DEATH`, `SAWGOD_BATTLE_CAST_GLOW` to exactly `0`
turns that one off for the session. Sleep covers only effect-7 palette 1; death covers palette 4 and
its dark follow-up. Other shared palettes remain ordinary. All native art and coverage are preserved,
including ice's transparent cracks. Group padding: fire 8 native pixels (one tile); ice, thunder, poison, sleep
and death 4 px. Thunder uses #FFF080 light at 1.25x strength. Death uses the subtractive light path.
The casting glow (shared casting 0x378130) is white with 8 px padding; the delivery glow (native delivery states
0x378EA0, 0x3788B0 and 0x3784B0) has 8 px padding and takes its color from the traveling sprite's live palette.
Casting stays white. Delivery samples the traveling sprite's live raster colors: all nonblack
covered pixels contribute RGB weighted by brightness (3R + 6G + B). The mean's largest channel
is normalized to 1, preserving hue/saturation and the previous peak strength. An entirely black
sprite emits no delivery light. No per-spell color table is used; phase 3 is unchanged.
Native ice, sleep and death casts have no separate delivery state.
Connected native spell pixels form at most eight groups, merging the nearest centers when needed.
Each group's light is centered at its brightness-weighted mean and uses a soft disc whose radius
covers its extent plus the animation's padding (rounded up to 2 px). All sizes share one cached
texture. This replaces exact 8×8 pixel masks and per-mask distance textures. Spell art is restored
exactly and excluded from ambient
bloom extraction; the existing scene bloom remains independent. Moving delivery lights
light visible enemies/background/floor as they travel; UI stays protected. Separate breath/gaze/physical
animations, stone, WSC and GB stay unchanged. `SAWGOD_BATTLE_TRACE=1` logs native animation
returns and HP changes for hit-frame comparisons.

**GB style.** While the picture is in GB graphics (Graphics = GB, or the Swap track stepped down to GB), the
Options menu and the FF / Encounters badges are drawn black on white: white windows with a black line, text
without the font's shadow, gray where the WSC style dims. It switches with the Graphics row.

Enhanced worlds 88, 102, 122 and 136 (and the false paradise, map 74, which uses the world camera too) enter at the
settled north-up ground camera: starting a game,
leaving a town and returning from battle do not animate its tilt. Stable field action frames keep that
camera. A player fidelity switch still crossfades overhead and tilted views in 0.5 seconds. Tilt,
pull-back (1.5x distance) and effects use the same fade amount. Actors
stay upright; UI stays in screen space. Leaving the field drops the camera; returning restores its
settled view. Scripted layouts, dark rooms and legacy rendering use overhead
presentation. The wider
view samples the loaded map and its authored border, clamping outside it; the game does not wrap.
The GPU resolves the loaded 80×80 cell layout (160×160 quadrant words), packed 32 KiB tile RAM,
palettes, and water tags. Ground repair and field-ring/event edits change only the cell layout;
tile/palette animation uploads only those small textures. GPU-generated color mip levels with Lanczos
reconstruction smooth minified ground while magnified WSC texels stay crisp. The plane, standing cards,
water and grade composite at the game picture's output resolution; light/shadow fields and bloom use
the fixed smaller targets described above. Mode 7 ignores pixel snap;
flat Enhanced retains it. The ordinary sharply scaled UI is restored on top. Tagged landmarks and actors share depth-sorted, cached
textured cards. Suzaku ruined blocks stay flat; Tokyo Tower stands at its original height with its painted
shadow. Perspective water keeps tint/shimmer; shoreline reflection remains an overhead-only effect.

Enhanced non-world field maps use the same GPU tile cache and active actor cards with no tilt or focus,
zoomed out by `enhanced.field_zoom` (Field Zoom, default 1.25×). The renderer chooses the whole output-pixel size whose effective zoom is
nearest; every WSC pixel is an equal square, including actors. At 1353×870, 1.50× selects 4×4 pixels
and about 338×218 map pixels; at 1120×720 it selects 3×3 / about 373×240.
Whole-pixel quantization can choose the original scale at small window sizes. The original party
anchor follows the game's scroll, with the wider view clamped to the loaded map and authored border.
All qualifying live native NPC/vehicle records appear, including those outside the OAM draw cutoff.
The loaded eight-cell border bounds framing. Every eligible field can clamp near an extreme edge:
map IDs 1–87, 89–101, 103–121, 123–210.
Worlds 88/102/122/136 keep their fixed distance. Their tilted view sees past the loaded map: there the ground
repeats the outermost two authored border cells (keeping 2x2 border patterns), on the CPU and GPU alike; the
loaded grid's top two rows are cell 00 filler on every map and never show.

The zoom never moves the camera by itself. A field that comes on screen in
Enhanced is zoomed out from the first frame of its fade-in: entering from the world map or another
field map, loading a save, and returning from a battle or a full-screen menu. It stays zoomed under
dialogue, small native windows and Options (whose blurred backdrop is the zoomed picture); UI is drawn
on top in screen space as always. Only a fidelity step animates it: over the shared 0.5 seconds the
view zooms continuously about the party anchor between the ordinary picture's exact scale and framing
and the settled whole pixels (the scale is fractional only inside that fade, effects are unsnapped
there, and the GB picture stays registered with the map inside the game's own view). A scripted scene
that takes the ordinary field picture away mid-map (pans, the battle wipe, vehicles) still lowers the
zoom through the fade; dark/puzzle rooms and LCD keep the ordinary framing. GB/WSC
framing stays unchanged. Lights, water, bloom and shadows use the expanded field; Snap to Grid
quantizes effects to its projected WSC pixels. With Screen Mode IPS the zoomed field has pixel border
lines like the world map's: one output pixel on the right and bottom edge of every N×N block, at the
world grid's strength (at Enhanced a line multiplies by 1 − 0.5 × 2/3 × 115/255 ≈ 0.85), drawn over the finished picture
and never over UI, which keeps its ordinary IPS mask.
`--shot-mode7-steps` also exercises flat field zoom when given a non-world `--shot-map`;
`--shot-mode7-flat` forces the ordinary field picture.

`--shot-mode7-steps --shot-map ID,X,Y --shot-page none --shot-overlay PREFIX` captures a deterministic
181-frame camera/fidelity sequence (40 samples/s, intended for a half-speed 20 fps GIF). It uses the
native leader sprite and walking poses in a halted presentation fixture, moving the viewport three
cells east. Worlds 88/102 also advance their native six-frame water-palette callback.
`SAWGOD_FRAME_LOG` records Mode 7 CPU phase times in this accelerated fixture as well as live play. `--shot-field-party` includes that native sprite in an individual map still.

`--shot-run N` (with `--shot-overlay PREFIX --shot-page none`, usually `--inputs FILE --frames F` and a
`--shot-store DIR` save) plays the real game: after F frames it writes each of the next N game frames
to `PREFIX_NNN.png`, with the fades on a simulated 75.47 Hz clock, so real transitions (entering a town,
dialogue, a battle) run through the live camera deterministically. `--shot-step FRAME:DIR` (repeatable)
steps the fidelity down (-1) or up (1) at that game frame. `SAWGOD_FRAME_LOG` then holds one row per
game frame: frame, scene kind, map, camera amount, field loop, dialogue.

Lights overlap by maximum, with most glow within 16 native pixels and a faint tail ending at
32 pixels. Touching sign tiles share bright emitting pixels and a uniform emitting span; Pulse
uses saturated center gem pixels and gently lights the surrounding area. Bloom requires a small
local highlight, so broad bright surfaces such as snow do not emit it.

For accelerated field captures, `--shot-map-batch FILE` reads one `map,x,y,output.png` per line
and renders all views in one app process. Output paths are relative to FILE; blank lines and
lines beginning with `#` are ignored. Use `--shot-size` and the usual config settings. This mode
uses halted field fixtures with no menu; it cannot combine with individual shot/state fixtures.

The field camera holds through everything that happens while the field is on screen (scripts, item gets,
warps, vehicles, windows). Unsupported GPU/shader formats, battles, full-screen menus, and rewards
retain the ordinary picture; there are no per-map exceptions. Exact and Smooth pacing both enable
Enhanced effects and cameras. The GPU scene uses the current completed picture in either mode;
no picture is mixed with an earlier one.
Enhanced honors IPS: the flat picture and UI use the ordinary grid; Mode 7 ground and standing
cards use their own WSC texel boundaries in perspective, and the zoomed flat field its own pixel blocks. Lines fade as cells shrink below about three
output pixels. Off and LCD draw no grid at any level, Enhanced included; LCD at Enhanced is a wash over the picture (see `video.screen_mode`).
Mode 7 actors use the nearest whole output-pixel scale at their feet (minimum 1), stay upright, and
have soft ground-plane contact shadows controlled by Contact Shadows. Landmark cutouts pivot at their
visible bottom edge, excluding transparent ground padding. `--shot-mode7-flat` selects the ordinary
Enhanced world picture in accelerated `--shot-overlay` fixtures.
No game state, animation frame count, RNG or input timing changes.

`materials/landmarks.csv` carries a checksum of its extracted defaults. Intact generated files refresh
when built-in defaults change; edited or unstamped custom files are preserved with one startup warning.

Offline shader generation: `tools/compile_shaders.py --name identity|enhanced|mode7 --dxc PATH`, with DXC's
library directory on the platform library path. Normal builds need only the embedded outputs;
`--check` validates source/output hashes. SPIR-V, DXIL and MSL are embedded (MSL is generated
from the SPIR-V with SPIRV-Cross, `--msl-only`).

For a capture of the fidelity sequence, `--shot-fidelity-steps` with `--shot-map ID,X,Y`, `--shot-page none` and
`--shot-overlay PREFIX` emits PREFIX_000.png through PREFIX_180.png. It samples the same fade state and
GPU passes at 40 samples per game second; playing them at 20 fps shows Enhanced → WSC → GB → WSC → Enhanced
at half speed. The fixture halts gameplay.

`--frame-timing-log FILE` enables an opt-in CSV with one row per SDL presentation. The historical
`SAWGOD_FRAME_LOG=FILE` also enables it in normal runs. Start with Graphics locked to WSC, GB or Enhanced,
walk for about 20 seconds on a world map, then repeat in a town; use a separate file for each run.
The log records the actual output size, display refresh, vsync setting, graphics level, native and shown
frame IDs, inferred repeats/drops, native scroll deltas, held/paused pictures, scheduler resets, audio
queue/worker costs, ground atlas revisions and CPU timing for each render pass. Graphics IDs are
0 = GB, 1 = WSC, 2 = Enhanced. `dropped` counts produced frames omitted between shown frames, including
intentional Fast-Forward (see `speed`); `repeated` counts repeated submitted pictures, including holds.
`pacing` is 0 = Exact, 1 = Smooth, 2 = Lo-Spec; `screen` is 0 = Off, 1 = LCD, 2 = IPS.
`mode7_drawn` confirms a successful camera draw, beyond the requested `mode7` fade amount;
`scene` is 0 = Unknown, 1 = Field, 2 = Battle. `battle` and `full_ui` identify the current UI owners.
`interpolation` is the completed-state fraction, `from_frame`/`to_frame` identify its endpoints,
`snapped=1` means the current endpoint was submitted without interpolation. `scroll_x/y` describe
the submitted camera (native pixels); `origin_x/y` give the field viewport offset. Repeated picture IDs
can have different Smooth positions. `presentation_time_ms` uses real completion timestamps in play
and simulated display timestamps for `--shot-display-hz HZ`; `t_ms` always remains measured CPU time.

`--frame-picture-log FILE` adds measurements from the final rendered RGB composite, read before
SDL_RenderPresent. `pixel_ground_dx/dy` measure local block displacement at the logged output-pixel
`ground_patch_x/y`; `pixel_ground_x/y` accumulate it from that patch's initial center. An invalid match starts a new `ground_segment`;
positions in different segments have different reference features. A dry patch near
the party's ground height is preferred. `pixel_party_x/y` register the party's visible pixels against
separate templates for each art key, initialized at that art's raster foot anchor. Thus animation shape
changes do not become motion. `geometry_ground_x/y` and `geometry_party_x/y` are independent geometry
references, in output pixels; compare them with the measured pixels. Measurements are integer output
pixels; compression, perspective gradients, changing light/water, clipping and occlusion can invalidate
registration. Each `pixel_*_valid` and `pixel_*_error` describes that match; invalid coordinates are `nan`,
never a fabricated zero displacement. The first sample and first appearance of each art key are invalid.
`picture_hash` hashes the full final RGB picture and detects actual duplicates, independently of frame IDs.
`pixel_probe=0` identifies an ordinary timing-only log without readbacks.

This diagnostic synchronizes the GPU and can change pacing. `picture_readback_ms` includes that wait
and pixel matching separately from `present_ms`. These are submitted composite measurements, not panel
scanout measurements; a compositor can still repeat or drop them. Use a short capture and retain a
screen recording if the failure changes when readbacks are enabled.

The named pass `*_ms` columns measure **CPU command submission**. In a bundled-SDL Vulkan build,
`gpu_ms` separately reports hardware timestamp duration from before scene encoding through the queue
submission's final command, including uploads, GPU copies and the presentation blit. It is polled without
waiting from a 16-slot ring: `gpu_present_id` identifies the measured earlier `present_id`; never attach
it to the current row's picture. `gpu_valid=0` and `nan` mean no completed result (startup, queue backlog,
unsupported driver/system SDL); saturated rings skip measurements rather than blocking. The last pending
frames may have no result when the process exits. Timestamp reset/reuse is protected by the submission
fence. D3D12/Metal/legacy renderers currently have no timing bridge.

`gpu_over_budget_count` counts completed GPU durations exceeding one nominal refresh budget, assuming
refresh rate stays constant over the query delay. It is not a missed-scanout count. SDL exposes no actual
scanout/compositor refresh accounting, so `scanout_missed_refreshes=-1` explicitly means unknown.
Timing-only logs insert timestamps and flush command encoding but introduce no diagnostic GPU wait.
The CPU `flat_composite_ms` / `projected_composite_ms` columns include the separate vertical bloom pass.

`present_ms` measures the CPU wait
inside SDL_RenderPresent; completion-to-completion `frame_ms` is not a physical panel scanout timestamp.
A display may repeat a submitted picture between calls without SDL reporting it. The timing-only log adds no diagnostic
GPU synchronization. The explicit picture log does. `accelerated=1` denotes screenshot/measurement runs: their
`present_ms` is an offscreen readback/fence wait and their intervals measure throughput, not display pacing.
`--shot-field-walk` produces 49 one-pixel viewport steps at a fixed graphics level; `--shot-timing-only`
keeps the full rendering and a one-pixel fence readback but omits PNG output. Both are accelerated
fixture controls, used with `--shot-overlay PREFIX --shot-map MAP,X,Y --shot-page none`. `--shot-native-npcs`
restores the prior OAM-only camera actors for a before/after fixture; it is not a saved gameplay option.


### Smooth motion

Lo-Spec uses the same latest-two-step interpolation at 30 presented frames per second. On 60/90/120 Hz
panels, its cadence spans 2/3/4 refreshes; other refresh rates and non-vsynced rendering use a 33.3 ms
wall-clock cadence. Input is pumped between presentations, and native ticks and audio continue on the
75.471698 Hz clock. Enhanced scene rasterization caps at 3x (672×432), while host overlays retain output
resolution. Enhanced music uses 16 voices per synth instead of 32 (from the next song); audio buffers
do not change.

Smooth is the default on every platform. Existing saved values are kept. Select Smooth under Options → Video → Performance or set `video.pacing = "smooth"`.

The native clock remains 3,072,000 / (159 × 256) = 75.471698 Hz. Smooth removes the game-tick wait
from Enhanced presentation: vsync controls submissions; without vsync, each render samples elapsed
real time. Variable refresh uses that same real clock, not a configured nominal display rate.
No display time enters the native machine, input recording, RNG, audio or HostPicture publication.
The interpolation interval is the latest two completed native steps; the fraction clamps to [0,1],
so a slow renderer never predicts the next step. Audio remains on the existing game-step path.

Only field camera/party/NPC/vehicle positions interpolate. Actor identity is the live record address
(the player uses its native record and tile-bank subcard), not OAM slot or animation-art cache key.
Moving NPC positions retain the native record's half-pixel precision; idle cards keep their
integer positions. Animation poses, spawn/hide state, tile animation, palettes, effects clocks, fades, battles, text,
menus and all host UI use the current completed picture. Stock maps reject edge movement and do not
wrap; the motion helper supports shortest-path periodic coordinates when a wrap period is supplied.
The ground retains nearest magnified pixels and its existing minification filter. Sprite feet round
to the output lattice (at most 0.5 output pixel error per axis); field scale stays whole output pixels.
Fractional scrolling shifts that lattice rather than flooring the camera to native pixels.
While any rendered position is fractional, the field effects pass samples the GPU geometry directly;
it does not re-quantize the finished picture on a fixed native lattice. At integer endpoints the
snapped effects grid returns. Static
framing is identical to Exact at the same field zoom; no temporal color blending is added.

Map/load/reset/generation/scene/owner changes, palette-fade boundaries,
menu/dialogue handoffs, fidelity switches, scripted layouts, jumps larger than a walking step,
stops, turns and travel-mode changes invalidate interpolation. Small camera steps must also match
the native pending movement vector (including its final step); an idle authored camera jump snaps. New/removed NPCs publish immediately; individual NPC
jumps larger than their live native velocity step snap. Fast-forward and speed settings above 1 use endpoints;
Options pauses do not interpolate across the pause, and the existing accelerated one-step capture
uses an endpoint unless an explicit display clock is requested.

Motion is delayed by at most one game step (13.25 ms), at most 1 native pixel for ordinary walking
or 2 pixels at native dash speed, up to 4 pixels in the native fast travel mode. NPC lag is at most
one native velocity step (half-pixel precision), and a larger step snaps. There is no additional
smoothing lag. At stop/turn/collision the current endpoint publishes immediately, so endpoint catch-up
can be up to one walking step. **Cell glide :** during a committed 16-pixel cell move the
displayed camera runs at the cell's average speed, 16 px per (steps + 1) game ticks (walking 17 ticks, the bike 9,
the glider 5, as traced from the game), so the game's tick without a step no longer shows as a pause. It never
passes the place an on-time game would show and stays at most one step behind; the party and its vehicle ride the
camera. A graphics switch or fade-in mid-cell resumes the glide. Native response to released input still waits
for the game's current tile move to complete, as in Exact. `SAWGOD_MOTION_TRACE=FILE` (opt-in) writes one line per game
tick and one per present.

`--shot-display-hz 60|120|144|240` with `--shot-run N` samples N presentations on a deterministic
clock through the same app tick/collection/upload/draw path. It is a test control, not a saved
setting. Capture CSVs distinguish simulated presentation times from measured software GPU throughput. Physical scanout timestamps
are not available through the existing SDL renderer, so hardware logs need a manual review.

The party's two native walking tile banks share one motion identity (`D520`); they are alternating art
buffers. Projected actors have their own fixed IPS grid; the ground-grid pass skips their covered pixels.
Outgoing native scripts keep the camera of the published field until the next picture takes ownership.

Options pages are Video, Audio, Keyboard, Gamepad, Gameplay, Cheats and System (plus Debug in development builds).
Gameplay’s 13 rows fit one screen. Battle Transparency is hidden and fixed at 30%.
System lists Language, Reset to Default Settings, Reset Game and Quit Game. Reset has no input binding.
Binding pages list the WSC buttons,
then Options, Encounters On/Off, Toggle Minimap, Mode Down, Mode Up and Fast Forward. Config IDs stay unchanged.
Graphics/Music Swap help uses the current device’s bound Mode Down/Mode Up controls and selected pad icons.

The Gamepad page shows **Touch Controls: Auto / On / Off** on Android or with the desktop
`--touch-controls` debug switch (which also enables mouse testing). It is saved as `input.touch_controls`.
Auto (default) shows touch only without a real SDL-recognized controller; connect/disconnect takes effect
immediately without pausing. The overlay's own joystick and unmapped joysticks do not count. On keeps
touch visible alongside every controller, and Off hides it. Keyboard and all gamepads always work.
No controller event overrides the saved choice. Desktop normally has no touch or type row.
Options keeps the hamburger, X pad, A and B visible for closing, navigation, accept and back.
Y, Mode and START are hidden. Binding capture ignores the touch joystick; the hamburger cancels capture. Android Back performs the same Options action even under Off, regardless of bindings.
Hiding touch releases held virtual buttons. The picture and Options keep their normal fit-to-window
size with touch on or off. All control sizes scale with window height (at 1080 high: pads 320 px,
A/B 172 px, top circles 120 px, START 240x120 px), with floors of 57 dp for pads/A/B and 48 dp
for the top row. Controls stay inside the safe area and overlap the picture when bars are narrow;
they never shrink to fit the bars.
Prompts follow the last device pressed; Auto icons use the last real controller's family when available,
with the usual Xbox fallback when only touch is available.

The touch Mode button cycles unlocked graphics/music GB -> WSC -> Enhanced -> GB with a fade.
Locked tracks remain fixed. Mode Down/Up keyboard/gamepad bindings continue to clamp at the ends.
