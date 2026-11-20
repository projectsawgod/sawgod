# wsc-re-kit

A standalone WonderSwan / WonderSwan Color reverse-engineering pipeline: identify
cartridge geometry, collect code coverage, discover code, import it into Ghidra,
and generate GNU assembler source that rebuilds byte-identically. Copy this whole
directory anywhere; it has no dependency on its parent project. Code is CC0; see
[LICENSE](LICENSE).

## Requirements

- Python 3.10 or newer and `python -m pip install capstone` (tested with 5.0.7).
  Other Python dependencies are from the standard library.
- GNU binutils with the i386 target: `as`, `ld`, `objcopy` on PATH. Encoding hints
  require a recent GNU assembler (tested with binutils 2.42). On Windows use
  MSYS2's GNU tools and a POSIX shell for the shell wrappers.
- Ghidra **12.1.4 PUBLIC**, its supported JDK (tested with JDK 21), and
  `GHIDRA_HOME` pointing at that installation. Scripts use `x86:LE:16:Real Mode`.
- A portable [Mesen2 distribution](https://github.com/SourMesen/Mesen2/releases)
  with WonderSwan support, if you want runtime coverage. Mesen2 is not included.
  Headless capture needs the portable executable and its companion native
  libraries, plus the platform libraries required by Mesen2.
- Your own unmodified `.ws` or `.wsc` ROM. No ROMs, cartridge data, or BIOS are
  distributed here. Modified ROMs with a recognized footer also work; a checksum
  mismatch is only fatal with `romid.py --strict`.

## Pipeline and worked example

Commands below run from this directory. Replace the placeholder paths. Use an
absolute ROM path for Ghidra and keep generated output in a separate directory.

```sh
ROM=/path/to/cartridge.wsc
OUT=/path/to/work
export GHIDRA_HOME=/path/to/ghidra_12.1.4_PUBLIC
mkdir -p "$OUT"
python3 tools/romid.py "$ROM" --strict
```

1. **Collect a CDL.** In Mesen2's GUI, open the ROM, open the WonderSwan debugger,
   and play through the parts you want covered. The debugger's Code/Data Logger
   menu provides Reset, Load, and Save CDL actions. Save a `.cdl` file. Coverage
   from one play session cannot reveal every routine. Alternatively, collect a
   bounded accelerated headless run:

   ```sh
   python3 tools/mesen_run.py "$ROM" --mesen /path/to/Mesen \
     --frames 600 --input 120:start --input 126: --out "$OUT/capture"
   ```

   `MESEN` can supply the executable path instead. The runner copies the portable
   executable and companion libraries into a temporary session, enables Lua file
   access there, and leaves your installed settings, saves, and debugger workspace
   alone. Outputs are `cdl.txt`, `ram.bin`, `mesen.log`, and the generated Lua script;
   `capture.cdl` is retained when Mesen2 saves its native debugger log. Buttons are
   `up/down/left/right`, `up2/down2/left2/right2`, `start`, `a`, `b`, `sound`.
   Inputs specify held buttons until the next change; an empty value releases all.

2. **Normalize and merge logs.** GUI `.cdl` files and headless `.txt` files can be
   mixed. This validates native CDLv2 CRC32s and removes Mesen2's leading padding
   for non-power-of-two ROM sizes.

   ```sh
   python3 tools/cdl.py "$ROM" "$OUT/capture/cdl.txt" --out "$OUT/merged.txt"
   # Or: python3 tools/cdl.py "$ROM" /path/to/session.cdl --out "$OUT/merged.txt"
   ```

   Text format: `HEX_FILE_OFFSET DECIMAL_FLAGS`, one touched byte per line;
   bit 0 means **opcode start**, bit 1 means data read. Optional extra columns are
   ignored; `#` starts a comment. Native Mesen2 logs flag operands too: normalization
   decodes code runs and keeps explicit branch/function entry markers. Inspect
   boundaries for overlapping code or unusual V30 instructions. Headerless binary
   CDL is intentionally rejected (no reliable identity/format check).

3. **Discover code.** Reset is always a seed. Add known interrupt entry points as
   needed; vectors are installed into RAM at runtime and cannot be inferred from
   the ROM footer.

   ```sh
   python3 tools/codeflow.py "$ROM" "$OUT/merged.txt" --out "$OUT/code.txt"
   # Example extra entry: --seed F000:C000 (hexadecimal CS:IP)
   python3 tools/rommap.py "$ROM" --cdl "$OUT/merged.txt" --code "$OUT/code.txt"
   ```

   Output is `HEX_FILE_OFFSET DECIMAL_INSTRUCTION_LENGTH`. `tables.txt` beside it
   lists speculative word-table locations/counts only when `--jump-tables` is
   enabled. That heuristic is off by default; indirect branches remain unknown.

4. **Import into Ghidra and mark covered code.** `PYTHON` can override the Python
   interpreter used by the CDL wrapper.

   ```sh
   sh ghidra/import.sh "$ROM" "$OUT/ghidra" cartridge
   sh ghidra/cdl.sh "$ROM" "$OUT/merged.txt" "$OUT/ghidra" cartridge
   ```

   The project contains internal RAM, the SRAM window when applicable, power-on
   ROM windows, and a `bank_XX` overlay at CPU `20000` for every physical bank.
   Small-ROM linear aliases are mapped individually. EEPROM is port driven and
   is not mislabeled as SRAM. All wrappers use `GHIDRA_HOME` from the environment.
   Import replaces an existing program of the same ROM filename in that project.
   Broad auto-analysis is off so it does not guess code throughout data banks;
   enable selected analyzers later in Ghidra. Generic x86 analysis does not fully
   model NEC V30MZ instructions, port effects, or bank switching.

5. **Add symbols (optional).** Both Ghidra and the disassembler accept the same
   UTF-8 whitespace-separated format. Offsets are hexadecimal; sizes are decimal.
   Names use letters, digits, and underscores and start with a letter/underscore.
   Names and addresses must be unique. Comments start with `#`.

   ```text
   # kind file-offset-or-CPU-RAM-address name optional-size
   rom 1c000 startup 22
   ram 0100 frame_counter 2
   ```

   ROM addresses are always **file offsets**, avoiding ambiguity across switched
   windows. RAM addresses are CPU linear addresses below `20000`. Sizes are
   descriptive label metadata. Use `func` in place of `rom` to explicitly request
   a Ghidra function at that ROM offset. An end-of-line `#` comment becomes its
   plate comment. The disassembler emits ROM/function labels; Ghidra imports RAM
   labels too. Plain `rom` entries do not force code or data classification.

   ```sh
   sh ghidra/symbols.sh /path/to/symbols.txt "$OUT/ghidra" cartridge
   ```

6. **Generate and rebuild.** Leave off `--symbols` if you have no symbol file.

   ```sh
   python3 tools/disasm.py "$ROM" "$OUT/code.txt" --out "$OUT/asm"
   python3 tools/build_rom.py "$ROM" "$OUT/asm" --out "$OUT/rebuild"
   ```

   `bank_XX.s` contains Intel-syntax code and labels. Data, unclassified regions,
   and unsupported instruction encodings use `.incbin "baserom.wsc", offset, length`;
   no raw ROM byte arrays are embedded. The generated linker script puts every
   64 KiB bank at VMA zero, sequentially in the output image. Each command compares
   the entire rebuilt image with the supplied ROM and fails on any difference.
   To publish source, distribute `bank_*.s`, `rom.ld`, and `include/macros.inc`;
   omit `baserom.wsc`, `rom.wsc`, object/ELF files, captures, and Ghidra databases.
   A matching rebuild proves byte preservation, not correct code classification
   or relocatability. Pointer inference and automatic data relocation are outside
   this kit's scope; direct near code branches receive labels where safe.

## Mapping limits and other tools

The footer supplies ROM size, save type, mapper byte, system flag, and checksum.
Recognized sizes range from 128 KiB to 16 MiB, including 3 MiB and 6 MiB images.
The supported model is the standard mapper used by footer mapper values 0/1;
unknown values and size/file disagreements fail explicitly. Flash cartridges,
special cartridge peripherals, BIOS execution, and executable RAM need separate
analysis. A footer does not describe every physical cartridge feature.

Code discovery defaults to mapper registers C0/C2/C3 = `ff`. Override with
`--linear-page`, `--bank0`, and `--bank1` (values use Python integer notation,
e.g. `0x03`). A CDL seed outside the chosen linear page is analyzed through a
ROM0 context for its physical bank. Near control flow retains CS, including
nonaligned segments and 16-bit IP wrap. Far branches preserve their explicit
CS:IP. A CDL has no CS history: its seeds assume a 64 KiB-aligned CS at the chosen
window; add explicit `--seed CS:IP` entries for known nonaligned contexts.
Writes to mapper ports are **not simulated**: run separate analyses with
the needed register values and collect more runtime seeds. Ghidra's primary map
always shows power-on registers; use its bank overlays for switched code.

The assembler output chooses the earliest decode when instructions overlap.
Instructions spanning a physical 64 KiB bank boundary remain opaque data in the
generated source. Both cases preserve the image but need manual analysis if you
intend to edit or relocate that code.

`tools/asm_roundtrip.py ROM OFFSET LENGTH [--vaddr OFFSET]` diagnoses GNU as
encoding differences in a linear sweep. Unlike `disasm.py`, it reports encodings
that need an opaque fallback rather than producing a complete image.
`tools/cyclecost.py TRACE_A TRACE_B [--rom ROM]` compares mean cycle deltas by
CS:IP. Trace rows are `DECIMAL_CYCLES HEX_CS:HEX_IP ...`; `frame...` resets the
previous instruction. Deltas above 60 cycles and backwards counters are excluded;
interrupts, wait states, and DMA still require interpretation.

See [hardware notes](docs/HARDWARE_NOTES.md) for the main debugging pitfalls.
Mesen2's [source](https://github.com/SourMesen/Mesen2) documents the native WS CDL
format and portable test runner used here; the cartridge footer and bus layout
are described in the [WSdev memory map](https://ws.nesdev.org/wiki/Memory_map).

## Tests

```sh
python3 tests/test_pipeline.py -v
# Or run the standalone CTest group:
cmake -S . -B /path/to/kit-tests -DPython3_EXECUTABLE=/path/to/python
ctest --test-dir /path/to/kit-tests -L wsc-re-kit --output-on-failure -j6
```

Tests generate synthetic ROMs and CDL data locally: all recognized sizes, footer
validation, SRAM/EEPROM types, mirrored mappings, far/near flow, nonaligned CS,
CDLv2 padding/CRC checks, symbol labels, opaque encodings, and independent image
rebuilds. No game data or emulator download is required.
CTest skips the synthetic suite if capstone or GNU binutils are unavailable (for
example, when a host only has Apple's assembler); install the requirements to run it.
The optional Ghidra test runs with `GHIDRA_HOME` set; otherwise CTest marks it
skipped. It checks two synthetic sizes, SRAM/EEPROM mapping, CDL, symbols, and an
explicit function import in Ghidra itself.
