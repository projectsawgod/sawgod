# WonderSwan hardware notes

These are analysis notes, not a complete hardware specification. The descriptions
are grounded in this project's CPU, video, audio, DMA, and interrupt implementation
and in [Mesen2's WS core](https://github.com/SourMesen/Mesen2/tree/master/Core/WS).
Use the [WSdev memory map](https://ws.nesdev.org/wiki/Memory_map) and the relevant
hardware register references when analyzing a cartridge with different hardware.

## CPU, cartridge mapping, and saves

The NEC V30MZ has a 20-bit address space and 16-bit CS:IP addressing. Linear
addresses are `(segment * 16 + offset) & fffff`; a far pointer's segment need not
be aligned to a 64 KiB boundary. Near control flow keeps CS and wraps IP at 16
bits. Generic x86 disassemblers do not implement every NEC instruction.

Internal RAM occupies the bottom of the address space: 16 KiB on monochrome WS,
64 KiB in WSC color mode. Cartridge space is controlled by the mapper. With the
standard mapper, `10000–1ffff` is the SRAM window (C1 selects its bank),
`20000–2ffff` and `30000–3ffff` are independent 64 KiB ROM windows (C2/C3), and
`40000–fffff` is the upper 768 KiB of the C0-selected 1 MiB page. Power-on mappings
used by these tools set all four bank registers to `ff`. The reset entry is at
CPU `ffff0`, near the end of the ROM. Small ROMs mirror; non-power-of-two dumps
need explicit treatment of missing leading capacity. Mesen2 pads at the beginning,
so its absolute ROM/CDL addresses can differ from unpadded file offsets.

The footer distinguishes SRAM and cartridge EEPROM. SRAM is memory mapped and
battery backed; EEPROM uses serial command/data ports (cartridge C4–C8). Console
EEPROM is separate hardware and must not be confused with a cartridge save.
Do not infer a save format from the hardware type: slots, checksums, and record
layout are software conventions. Example from this project: save management
copies structured records to SRAM; a memory snapshot alone does not establish
which records the game will accept after boot.

## Sound

The sound unit has four wavetable channels, with 32 four-bit samples per waveform
in internal RAM. Frequency divider ports are `80–87`; left/right volume nibbles
are at `88–8b`, and `8f` selects the waveform area. Channel 2 can act as a direct
voice output, channel 3 supports sweep, and channel 4 can use noise. Control/output
ports include `90/91`; headphone/output state affects the resulting signal.
WSC also supports sound DMA. A music driver is software that sequences notes,
envelopes, effects, and register writes; it is not a separate sound processor.

Example from this project: a VBlank handler services the music/effect sequencer
once per frame, updates waveform RAM and channel registers, and preserves the
interrupted registers. Per-frame sound-driver updates and per-sample sound-unit
clocks are distinct. Advancing one without the other changes playback. Repeated
register writes, divider edge timing, noise state, sweep reloads, and output
filtering can matter even when the note sequence looks correct.

## Tiles, sprites, and palettes

The display is 224×144 pixels. Two scrolling tile planes use 32×32 maps of 16-bit
entries. Tiles are 8×8 pixels, with palette, flip, priority/window behavior supplied
by the map/sprite attributes. The screen map bases are selected by port `07`;
the maps and graphics live in internal RAM, not in a separate VRAM address space.
Tile interpretation depends on the video mode: monochrome/2bpp, WSC 4bpp planar,
and WSC 4bpp packed layouts must not be decoded interchangeably.

The sprite table holds up to 128 four-byte entries; at most 32 sprites are selected
for a scanline. Ports `04–06` select the table base, first entry, and count. Sprite
attributes include tile, palette, priority, flips, and window selection; X/Y wrap
at eight bits. Updating table RAM and the displayed/latching sprite state are
different events. Example from this project: comparing sprite RAM at an arbitrary
instruction gave misleading differences until comparisons respected VBlank timing.

WSC color palettes occupy RAM `fe00–ffff`: 16 palettes of 16 little-endian 12-bit
RGB colors. Monochrome palette/shade selection uses I/O registers instead. Color
index zero is not universally transparent: backdrop, plane, sprite, window, and
mode rules all matter. Example from this project: palette writes and raster
changes needed scanline-aware rendering; a single end-of-frame RAM dump lost
mid-frame appearance changes.

## Interrupts, timing, and DMA

The system clock is 3.072 MHz; a scanline takes 256 clocks. With 159 lines per
frame, refresh is about 75.47 Hz. Visible lines are 0–143 and VBlank starts at
line 144. The last-line register is programmable, so hard-coding a host 60 Hz
frame clock is incorrect. CPU progress, display progress, and audio progress need
the same time base when comparing emulators or translated routines.

The interrupt base/vector register is `b0`, enable mask `b2`, pending status `b4`,
and acknowledge port `b6`. The interrupt vector table is in RAM. Relevant sources
include line compare (bit 4), VBlank timer (5), VBlank (6), and HBlank timer (7).
The HBlank timer counts scanlines, not CPU instructions; the VBlank timer counts
frames. Ports `a2–ab` control timer modes, reloads, and counters. Acknowledge
behavior, masks, priority, IF, and instruction boundaries determine when a handler
actually runs. Example from this project: interrupt-aware comparisons needed a
shared entry boundary and allowance for the pushed FLAGS/CS/IP stack words.

WSC general DMA uses source ports `40–42`, destination `44–45`, length `46–47`,
and control `48`. Transfers use words and stall the CPU; cartridge accesses can
cost more than internal RAM accesses. DMA completion also changes address/length
registers. Copying the bytes while ignoring the stall changes interrupt timing.
Example from this project: synchronous translated copies needed separate timing
accounting to remain comparable with an interpreter.

## Practical pitfalls

- CDL is evidence of execution/access, not a complete code/data partition. An
  opcode may also be read as data; interrupt handlers, indirect calls, overlays,
  and unvisited branches need additional seeds and context.
- A file offset, a CPU linear address, and a CS:IP pair are different identifiers.
  Bank switches can change the first while preserving the other two.
- Never assume all code is in the power-on top page, all pointers share a segment,
  or a RAM copy destination identifies an asset format for every game.
- GNU as can shorten immediates, choose another equivalent opcode, drop redundant
  prefixes, or relax a branch. A readable mnemonic is not a byte-preservation
  check. Compare the entire rebuilt image and keep opaque forms as `.incbin`.
- RAM, registers, mapper state, interrupt phase, DMA stalls, and sound-unit state
  are stronger comparisons than screenshots. A matching screen can conceal a
  timing error; a different screen can reflect only a different capture phase.
