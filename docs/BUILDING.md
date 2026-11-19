# Build and test

## Desktop setup

Use CMake 3.24 or newer, a C++20 compiler, Ninja for the Ninja presets, and Python 3.12.
Linux presets select `clang++`; Windows uses Visual Studio 2022/MSVC or the MinGW cross preset;
macOS uses Apple Clang and targets Apple Silicon/macOS 11 or newer. CMake fetches pinned SDL3,
Dear ImGui, miniz, 7-Zip and doctest sources on the first configure. Linux also needs the development
headers for the SDL audio/video backends you want to use.

Install the Python tool dependencies in `tools/.venv`:

```sh
python3 -m venv tools/.venv
tools/.venv/bin/python -m pip install -r tools/requirements.txt
```

On Windows use `tools/.venv/Scripts/python.exe`. Activate the environment before configuring so
CMake's Python interpreter can import these dependencies. Per-OS tool paths are in
`tools/env.sh` and `tools/paths.py`.

```sh
. tools/.venv/bin/activate
cmake --preset linux-release
cmake --build --preset linux-release -j 4
ctest --preset linux-release -j 4
```

Other desktop presets: `linux-debug`, `windows-msvc`, `windows-mingw` (cross-build from Linux),
and `macos-release`. See [Android](ANDROID.md) for its SDK, build and signing setup.

Native Windows test build, `windows-local`: MSYS2's MinGW-w64 toolchain (`pacman -S
mingw-w64-x86_64-{gcc,cmake,ninja,python-numpy}`) with `C:\msys64\mingw64\bin` first on `PATH`; the exe is
static like the release. Copy the release's `libfluidsynth-3.dll` beside `sawgod.exe` and the tests, and run
them without MSYS2 on `PATH`, or they load MSYS2's own FluidSynth (it crashes in PortAudio). The ROM tests need
`SAWGOD_WSC_ROM`. This build runs the real Direct3D 12 path, which Wine does not. To regenerate shaders here:
`tools/compile_shaders.py --all --dxc PATH/TO/dxc.exe --spirv-cross PATH/TO/spirv-cross.exe`.

## ROM and audio resources

The game needs the supported WSC dump: CRC32 `1B6F5F30`,
SHA-1 `6B7A9B810C932974CBDA4DE7178CF0EA5A90E628`. Start the built `sawgod` executable and choose the ROM, or pass
`--rom PATH`. First run stores a verified copy and builds `wsc.sgz` and `gb.sgz`; GB mode uses
ORIJINARU in the WSC cartridge and needs no separate GB ROM.

Enhanced music also needs FluidSynth at run time and the GeneralUser GS soundfont. Development
builds use `ref/soundfonts/GeneralUser-GS.sf2` (`python3 tools/release/fetch_soundfont.py` downloads it) as a fallback, configurable through
`SAWGOD_DEV_SOUNDFONT`; installed data paths and overrides are in [CONFIG.md](CONFIG.md).

## Checks

CTest uses dummy SDL audio/video through the test presets. Without ROM environment variables,
ROM-gated tests skip. For the complete ROM-gated suite, set these to your own files before CTest:

```sh
export SAWGOD_WSC_ROM="$PWD/ref/rom/Makai Toushi Sa-Ga (Japan).wsc"
export SAWGOD_GB_ROM="$PWD/ref/rom/Makai Toushi Sa-Ga (World) (Ja) (Rev 1) (Collection of SaGa).gb"
export SAWGOD_ROM="$SAWGOD_WSC_ROM"
nice -n 10 ctest --preset linux-release -j 4 --output-on-failure
```

The separate GB ROM is a test reference, not a game dependency. The GB opening art test also uses
`SAWGOD_ROM` and an existing `<data>/gb.sgz`; run the game once to create that archive, and set
`SAWGOD_HOME` if it is in a different data folder.

For a change, build and run the unit suite; format changed handwritten C++ with `clang-format-18`
and run `python3 tools/name_scan.py`.
