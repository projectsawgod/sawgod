# Licenses and credits

SawGod's own code is public domain (CC0 1.0, [LICENSE](LICENSE)). SawGod contains no game data: players supply their own
ROM, and everything taken from it is extracted on their own machine.

*魔界塔士 Sa・Ga* (Makai Toushi SaGa), *The Final Fantasy Legend* and *SaGa* are trademarks of Square Enix.
SawGod is an unofficial fan project, not affiliated with or endorsed by Square Enix or the original game's other
rights holders.

## Third-party components

These are built into, linked with or shipped beside SawGod. Full license texts are in [licenses/](licenses/).


| What | Where | License |
| --- | --- | --- |
| **Silver** pixel font by Poppy Works (https://poppyworks.itch.io/silver) | the ROM screen; glyphs baked into `src/engine/font_silver.inc` and `font_silver_prompts.inc` by `tools/font_bake_silver.py` | CC BY 4.0 |
| **Cinzel Decorative Bold** by Natanael Gama | English world-card PNGs baked from it; synthetic italic and small caps; no font shipped | SIL OFL 1.1; `licenses/CinzelDecorative-OFL.txt` |
| **Noto Serif CJK JP Bold** and **Noto Sans CJK JP Bold** (Adobe / Google) | the Enhanced title logo's kanji line (serif) and TM mark (sans), baked into `assets/ui/title_kanji.png` by `tools/title_logo_kanji.py`; no font shipped | SIL OFL 1.1; `licenses/NotoSerifCJK-OFL.txt` |
| **m5x7** pixel font by Daniel Linssen | variable-width text; `src/engine/font_m5x7.inc` | CC0 |
| **SDL 3.4.16** ([source](https://github.com/libsdl-org/SDL/tree/release-3.4.16)) | window, input, audio, GPU; static on desktop, shared on Android; Android Java activity support | zlib; `licenses/SDL3.txt` |
| **miniz 3.0.2** ([source](https://github.com/richgel999/miniz/tree/3.0.2)) | zip reading/writing (.sgz archives, mods); statically linked | MIT; `licenses/miniz.txt` |
| **7-Zip 26.01** C decoder (Igor Pavlov; [source](https://github.com/ip7z/7zip/tree/26.01/C)) | `.7z` ROMs on the ROM screen; decoder-only C subset, no unRAR | public domain; `licenses/7-Zip-C.txt` |
| **Dear ImGui 1.92.9b** ([source](https://github.com/ocornut/imgui/tree/v1.92.9b)) | the options overlay; statically linked | MIT; `licenses/ImGui.txt` |
| **FluidSynth 2.6.1** ([source](https://github.com/FluidSynth/fluidsynth/tree/v2.6.1)) | Enhanced music; replaceable shared library | LGPL-2.1-or-later; `licenses/FluidSynth-LGPL-2.1.txt` |
| **GCEM** (Keith O'Hara; revision `012ae73c6d0a2cb09ffe86475f5c6fba3926e200`; [source](https://github.com/kthohr/gcem)) | compile-time math in FluidSynth | Apache-2.0; `licenses/GCEM-Apache-2.0.txt`, `licenses/GCEM-NOTICE.txt` |
| **GeneralUser GS 2.0.3** soundfont (S. Christian Collins; [source](https://github.com/mrbumpy409/GeneralUser-GS)) | Enhanced music; bundled unchanged as `soundfonts/GeneralUser-GS.sf2` | GeneralUser GS License v2.0; `licenses/GeneralUser-GS.txt` |
| **GCC runtime libraries** | statically linked compiler support / C++ standard library | GPL-3.0 with GCC Runtime Library Exception 3.1; `licenses/GCC-runtime.txt`, `licenses/GPL-3.txt` |
| **MinGW-w64 runtime** | Windows compiler support, including static winpthreads | MinGW permissive notices and upstream exceptions; `licenses/MinGW-w64.txt`, `licenses/GPL-2.txt` |
| **Android NDK r29 LLVM runtime** | Android: shared libc++ and compiled compiler support | Apache-2.0 with LLVM Exceptions and preserved upstream notices; `licenses/Android-NDK-toolchain-NOTICE.txt` |
| **doctest** | unit tests only | MIT |

Silver by **Poppy Works / Wolfgang Wozniak** is used under
[CC BY 4.0](https://creativecommons.org/licenses/by/4.0/); full text:
`licenses/Silver-CC-BY-4.0.txt`. Source: https://poppyworks.itch.io/silver.
We rasterized glyphs into bitmap tables and arranged them in the ROM screen and prompts;
the original font file is not distributed. No endorsement is implied.
m5x7 by **Daniel Linssen** (https://managore.itch.io/m5x7) is rasterized for game text;
full CC0 text: `licenses/m5x7-CC0.md`.
SDL's bundled HIDAPI uses its original permissive license (`licenses/SDL-HIDAPI.txt`);
its yuv2rgb code uses BSD-3-Clause (`licenses/SDL-yuv2rgb.txt`), and its fdlibm math
notices are preserved in `licenses/SDL-libm.txt`.

Portable packages include the complete, unmodified FluidSynth and GCEM source archives in `source/`,
along with `build_fluidsynth.sh` and rebuilding instructions. FluidSynth uses its C++11 OS layer;
optional audio drivers and external DSP engines are disabled. Windows needs only OS DLLs;
Linux needs the OS's glibc/libm/loader and desktop/graphics/audio drivers. SawGod dynamically loads
the bundled FluidSynth library. You may replace it with an ABI-compatible build and reverse engineer
SawGod to debug modifications to that library, as provided by LGPL-2.1 section 6.

Android APKs carry the complete FluidSynth/GCEM archives in `assets/source/`,
`build_android_fluidsynth.sh`, the exact CMake options, and rebuilding/replacement instructions
(`docs/ANDROID.md`). Notices are in `assets/licenses/`. FluidSynth and SDL are separate shared
libraries; libc++_shared.so comes from NDK r29. No GLib or external FluidSynth audio drivers are bundled.


Each binary package includes one platform-specific `THIRD_PARTY_NOTICES.txt`, generated from
`tools/release/notices.py`, with versions and full-text paths. ImGui's modified stb headers use
their MIT option (`licenses/stb.txt`). The generated MSL helpers retain conservative SPIRV-Cross
Apache notices (`licenses/SPIRV-Cross.txt`); the compiler itself is not distributed. Cinzel's
OFL is retained for attribution; only pre-rendered lettering ships, not the font file.
`toolchain.txt` records desktop GCC/MinGW build versions; macOS system libc++ is not bundled.

ImGui also embeds ProggyClean and ProggyForever font data by Tristan Grimmer / Disco Hello
(MIT, `licenses/Proggy-fonts.txt`). Android preserves the NDK platform and LLVM runtime notices.

## Credits

SawGod was built with the help of these projects, people and documents. None of them ships with SawGod, and no
code was copied from them: emulators were run side by side as references, and documentation was read. Thank you.

### Emulators and debuggers

- [Mesen 2](https://github.com/SourMesen/Mesen2) by Sour: its WonderSwan core, debugger and test runner were the
  reference SawGod's decompilation was checked against.
- [Mednafen](https://mednafen.github.io/), through the [WonderfulToolchain fork](https://codeberg.org/WonderfulToolchain/wf-mednafen)
  with WonderSwan fixes and debugger improvements.
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra) (National Security Agency): disassembly and analysis.

### WonderSwan documentation

- The [WSdev wiki](https://ws.nesdev.org/) and its contributors.
- [WSMan](https://web.archive.org/web/2024/http://daifukkat.su/docs/wsman/), the WonderSwan hardware manual.
- [STSWS](http://perfectkiosk.net/stsws.html) hardware notes.
- [awesome-wsdev](https://github.com/WonderfulToolchain/awesome-wsdev), a curated list of WonderSwan development
  resources.

### SaGa research and fan projects

- [Tower Reversed](https://towerreversed.neocities.org/): the English translation patch for the WonderSwan Color
  game, and its script, character set and data dumps for the WonderSwan Color and Game Boy games.
- [GCGX](https://gcgx.games/): analysis of the WonderSwan Color and Game Boy versions (meat, monsters, items, bugs
  and the differences between them).
- [Data Crystal](https://datacrystal.tcrf.net/wiki/The_Final_Fantasy_Legend): *The Final Fantasy Legend* ROM and
  RAM maps.
- [ゲームカタログ@wiki](https://w.atwiki.jp/gcmatome/): notes on the WonderSwan Color version and its differences
  from the Game Boy original.
- [FFLRandomizer](https://github.com/eclipseyy/FFLRandomizer) by eclipseyy: Game Boy data layouts (monsters,
  items, shops, encounters).
- [saga1dx](https://github.com/akatsuki105/saga1dx) by akatsuki105 and
  [ffl1-color](https://github.com/gameboycolorizations/ffl1-color): Game Boy colorization projects and
  disassemblies.
- The *The Final Fantasy Legend* (Game Boy) bug-fix and text-restoration patch by Granz and Sarah, and *Final Fantasy
  Legend DX*.

### Inspiration

- The decompilation projects of [pret](https://github.com/pret), [Zelda Reverse Engineering Team](https://github.com/zeldaret)
  and [n64decomp](https://github.com/n64decomp), whose methods SawGod's decompilation follows.
- [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) by HarbourMasters: a native port built on a
  decompilation, with assets extracted from the player's ROM, mods and enhancements.
- [Denhonator](https://github.com/Denhonator)'s [Romancing SaGa 2](https://github.com/Denhonator/RS2UIMod) and
  [Romancing SaGa 3](https://github.com/Denhonator/RS3UIMod) UI mods, references for the interface and
  quality-of-life options.

### The original game

*魔界塔士 Sa・Ga* was created by Square in 1989 for the Game Boy, directed by Akitoshi Kawazu, with music by Nobuo
Uematsu; the WonderSwan Color remake was released by Square in 2002.
