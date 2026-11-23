# Cutting a portable release

From a Linux x86_64 host, install CMake >= 3.24, Ninja, Clang, GCC/G++, Python 3,
and SDL's Linux development dependencies (see `.github/workflows/ci.yml`). Windows cross builds
also need `g++-mingw-w64-x86-64` with its POSIX threading compilers. Python needs the project's
tool requirements (`tools/requirements.txt`).

Point `SAWGOD_REF` at your reference folder, containing `soundfonts/GeneralUser-GS.sf2`
(GeneralUser GS 2.0.3). No ROM is needed to build a release.

```sh
export SAWGOD_REF=/absolute/path/to/ref
tools/package.sh windows sawgod
tools/package.sh linux sawgod
```

Each command configures an isolated Release build, builds SawGod and a minimal **shared** FluidSynth
2.6.1, audits runtime dependencies, and writes a package plus SHA-256 checksum into `dist/`:

- `sawgod-windows-x86_64.zip`
- `sawgod-linux-x86_64.tar.gz`

`SAWGOD_PACKAGE_JOBS` controls parallelism (default 4; maximum 4). CMake fetches pinned dependencies;
optional offline source mirrors are `ref/deps/{sdl3,miniz,imgui,lzma7z}-src`.
FluidSynth and GCEM archives are copied from `SAWGOD_REF/fluidsynth/` when present,
otherwise fetched into this checkout's `ref/fluidsynth/` with pinned SHA-256 checks.

Packages include authored `gb_maps/`, `text/en.csv`, the soundfont, all licenses,
FluidSynth/GCEM source with the exact rebuilding script, and a file/import manifest.
They exclude ROMs, generated `.sgz` archives, saves, config and development paths.

Test the **unpacked archive**, with `SAWGOD_HOME`, `SAWGOD_ROM` and other `SAWGOD_*` overrides unset,
from an unrelated clean working directory. Launch with no arguments: the ROM screen must idle until
closed. Then launch with `--rom /absolute/path/to/original.wsc --shot-overlay /path/to/frame.png`:
check `rom/`, `wsc.sgz` and `gb.sgz` appear **beside the executable**. Relaunch with
`--shot-overlay /path/to/second.png` and no `--rom`; it must use the stored ROM. Check Enhanced audio
actually loads the bundled library and soundfont, not a host installation. Windows checks use Wine
with a clean prefix; dummy SDL video/audio drivers support headless runs. Run these checks with:

```sh
python3 tools/release/check_first_run.py windows
python3 tools/release/check_first_run.py linux
```

These also check the stored archives' integrity, a relocated installation, and non-silent synthesis
through the bundled library/soundfont. Logs, frames and timing results go under `tmp/release-*/`.

Linux's executable and FluidSynth statically include compiler runtimes; the archive bundles the
replaceable FluidSynth `.so` under `lib/`, found through `$ORIGIN/lib`. glibc and GPU/desktop drivers
come from the OS. Build on the oldest supported distribution to set the glibc floor; the initial
Mint 22 / Ubuntu 24.04 build requires glibc 2.39. Test on other supported distributions before publishing.
An AppImage is not currently produced; it would also need an AppImage runtime/tool and first-run
checks confirming OS user data storage (the existing `APPIMAGE` detection already selects it).

Before publishing, review licenses and English coverage, and test the unpacked packages on their
target platforms.

## macOS (Apple Silicon)

Build on an arm64 Mac with the Xcode command-line tools, CMake, Ninja and Python 3.12 plus
`tools/requirements.txt`. The `macos-release` preset targets arm64 and macOS 11.0 (the first Apple
Silicon release; SDL3/Metal support it). `tools/package.sh macos sawgod` creates
`dist/sawgod-macos-arm64.zip` and its SHA-256 checksum. `cmake --preset macos-release` builds
`SawGod.app`; packaging adds the pinned soundfont, replaceable FluidSynth dylib, source archives,
dependency audit, README and ad-hoc signature. Developer ID signing and notarization are optional
future steps, separate from this package command.

The bundle has its executable at `Contents/MacOS/SawGod`, private dylibs at `Contents/Frameworks/`,
and authored GB maps, English text, icon, soundfont and licenses in `Contents/Resources/`.
SDL resolves bundled resources independently of the launch directory. User data goes to
`~/Library/Application Support/sawgod/sawgod/` (or `SAWGOD_HOME`), including the stored ROM,
`wsc.sgz`, `gb.sgz`, saves, config and mods. The app bundle stays read-only after signing.

Enhanced, Mode 7, focus, influence, identity and Fast-Forward all use embedded MSL on Metal.
Generate the Metal portions from the checked-in SPIR-V without replacing SPIR-V/DXIL:

```sh
python tools/compile_shaders.py --all --msl-only --spirv-cross /absolute/path/to/spirv-cross
python tools/compile_shaders.py --all --check --spirv-cross /absolute/path/to/spirv-cross
```

The second command checks byte-for-byte reproducibility with that compiler, source/output hashes
and the SDL Metal resource tables. Without `--spirv-cross`, `--check` validates the checked-in
outputs and bindings without installing a shader compiler. Use the same SPIRV-Cross binary when
checking reproducibility; different releases may emit different source.

CI runs the macOS build, ROM-free unit tests and packaging. The macOS job fetches only pinned,
redistributable dependencies and the hash-verified GeneralUser GS soundfont, never a ROM.
The README explains opening an unsigned app through right-click > Open or System Settings >
Privacy & Security. On a Mac, test the downloaded/unpacked artifact from Finder:
launch, ROM import/relaunch, WSC/GB/Enhanced (including Mode 7), audio/Enhanced music, gamepad,
fullscreen and the user data location. Repeat after moving the app to Applications; no Homebrew
libraries should be needed at runtime.

## SawGod versioned releases and the one-time vanilla release

`.github/workflows/ci.yml` builds Linux x86_64, Windows x86_64 (MinGW on Ubuntu),
macOS arm64 and Android arm64-v8a. The existing Linux/Windows tests and macOS build/test
jobs gate the draft. Linux/macOS packaging reuses the tested build directories. All native
builds use at most four jobs, under `nice` where available. No job downloads a ROM.

Every push to `main` and every pull request runs the same builds and tests, with no release.
A pushed `v1.1.0` tag builds everything and creates a **draft**, never a published release.
For a manual retry, select the same existing tag and supply the version. The workflow checks
that the tag resolves to the checkout SHA and rejects a version with a partial platform selection:

```sh
# After the release checks pass:
git tag -a v1.1.0 -m 'SawGod 1.1.0'
git push origin refs/tags/v1.1.0
# The tag starts CI automatically. Use this instead only for a deliberate retry:
gh workflow run ci.yml --repo OWNER/REPO --ref v1.1.0 -f platforms=all -f version=v1.1.0
# A single-platform smoke run creates an Actions artifact, not a GitHub Release:
gh workflow run ci.yml --repo OWNER/REPO --ref main -f platforms=android
```

Use the release repository in place of OWNER/REPO. The workflow must be on that
repository's default branch for dispatch. Do not dispatch on top of an already running tag build.
Branch/tag pushes performed with `GITHUB_TOKEN` do not start a second workflow.

Artifacts: `sawgod-linux-x86_64`, `sawgod-windows-x86_64`, `sawgod-macos-arm64`,
`sawgod-android-arm64-v8a`. Each desktop artifact holds only the selected flavor and its `.sha256` file;
Android also records signing
status for the release body. A `v*` release attaches only the four SawGod packages and `SHA256SUMS.txt`
(five assets); the individual checksum files are used to audit the artifacts, not attached to the release.
Android's package is
`sawgod-android-arm64-v8a-release.apk`; "release" describes optimization, not signing trust.
Without all four signing secrets it uses a debug key and the body prominently says **TESTING BUILD**.
Partial signing credentials fail the job. See [ANDROID.md](ANDROID.md) for secrets and pinned SDK versions.
A retry may replace assets on an existing draft and removes assets outside its package list; it refuses
to change a published release.
Test the downloaded packages on their devices before publishing the draft. The tag/version flows through
`SAWGOD_RELEASE_VERSION` to the in-app version, numeric macOS bundle version and Android
versionName; ordinary development builds keep their dev label. Android versionCode comes from the
same version: major*10000 + minor*100 + patch (1.1.0 = 10100; minor and patch at most 99), so each release
installs over the last; untagged builds use 1 (as did the 1.0 build).

## Package license gate

The authoritative shipped-component inventory is `tools/release/notices.py`. Each package gets
one `THIRD_PARTY_NOTICES.txt` with component, version, license, attribution/modifications and the
path to every applicable full license text. The common inventory covers SDL3 (HIDAPI, fdlibm,
yuv2rgb), miniz, 7-Zip C, ImGui (including its modified stb headers and embedded Proggy fonts), FluidSynth, GCEM,
GeneralUser GS, Silver, m5x7, rendered Cinzel Decorative lettering and embedded shader outputs.
Linux/Windows add static GCC runtimes; Windows adds MinGW-w64/winpthreads. `toolchain.txt`
records their actual build versions. Android adds SDL Java and NDK r29 libc++/compiler support.
macOS imports Apple's system libraries; it does not redistribute them. OS libraries/drivers,
test-only doctest and build-only SDK/Gradle/compiler tools are not package payloads.

Silver credit names Poppy Works / Wolfgang Wozniak, links the original and CC BY 4.0, and
states our rasterization/arrangement changes. Cinzel Decorative is present only as rendered
lettering (including synthetic italic/small caps), with its OFL retained as a courtesy;
no font file ships. SPIRV-Cross is build-only; retain its Apache license/credits conservatively
for emitted MSL helpers. Authored shader outputs do not bundle shader compilers.
Sources: [Silver](https://poppyworks.itch.io/silver),
[OFL artwork FAQ](https://openfontlicense.org/ofl-faq/),
[SPIRV-Cross license](https://github.com/KhronosGroup/SPIRV-Cross/blob/vulkan-sdk-1.4.328.1/LICENSE).

Every enhanced-port package ships the exact hash-checked, unmodified FluidSynth 2.6.1 and GCEM archives,
rebuild options/script and replacement instructions. FluidSynth stays dynamically loaded and
replaceable; Android users can rebuild, replace and sign an APK with their own key. This uses
source distribution rather than a time-limited written offer. See LGPL-2.1 section 6 in
`licenses/FluidSynth-LGPL-2.1.txt`; audit changes to this linkage before release.
No GLib, external FluidSynth audio driver or optional external DSP library is bundled.

```sh
python3 tools/release/check_notices.py        # ROM-free inventory gate (also CI)
python3 tools/release/test_release.py         # release failure paths, no network
python3 tools/release/notices.py linux dist/sawgod-linux-x86_64
python3 tools/release/notices.py windows dist/sawgod-windows-x86_64
python3 tools/release/notices.py --apk dist/sawgod-android-arm64-v8a-release.apk
python3 tools/release/package_macos.py --list # planned app/archive staging list, no Mac required
```

Packaging fails before archiving/signing if a notice, README, required text or FluidSynth
source/rebuild file is missing. The Android APK is audited again after Gradle packaging;
macOS rejects unexpected non-system dylibs, and desktop runtime-import audits remain mandatory.
License auditing establishes the packaged inventory; it is not a substitute for legal advice.

## Original cartridge packages

With no flavor argument, each desktop `tools/package.sh` command builds and packages both SawGod and `sawgod-vanilla`, from the same tree:
`sawgod-vanilla-linux-x86_64.tar.gz`, `sawgod-vanilla-windows-x86_64.zip`, and
`sawgod-vanilla-macos-arm64.zip`, each with a SHA-256 checksum. They ship in a
separate one-time [release tagged `vanilla`](https://github.com/projectsawgod/sawgod/releases/tag/vanilla),
which will never be updated. Versioned SawGod releases do not include them; there is no Android vanilla
package. The macOS app is ad-hoc signed.

These packages contain the executable, short player README, commented default input mapping file, manifest
and applicable license notices.
They do not include extracted resources, English text, GB maps, a soundfont or FluidSynth. The original
Japanese menus and sound come directly from the player's supported ROM; raw SRAM goes beside that ROM.
The program runs SawGod's static recompilation (partially decompiled for readability), verified routine by
routine against the original by differential tests and fuzzing.

The `vanilla` tag (or a manual retry with `--ref vanilla -f platforms=all -f version=vanilla`) builds
both desktop programs on Linux, Windows and macOS, runs the usual desktop checks, and packages only
vanilla. Android is skipped. The draft is titled **sawgod-vanilla** and attaches the three packages plus
`SHA256SUMS.txt` (four assets). Its body comes verbatim from `tools/release/vanilla_release_notes.md`.
The same existing-tag and checkout-SHA checks apply to `vanilla` and `v*`; other tag shapes are rejected.
The `vanilla` tag is not passed to the numeric application-version settings.
Ordinary pushes and pull requests still build both desktop programs and Android, upload SawGod artifacts,
and do not draft releases.

For a local vanilla-only package without building the enhanced port or FluidSynth, use
`tools/package.sh linux vanilla` (or `windows vanilla`, `macos vanilla`). The default command without a
flavor produces both; use the explicit `sawgod` flavor for only SawGod.
Builds use the same presets; `sawgod-vanilla` does not link host UI, ImGui, Enhanced music or the Enhanced
renderer. CMake checks its dependency graph, and CTest checks the generated link command. With
`SAWGOD_WSC_ROM` set to the supported WSC ROM, CTest `vanilla_recording_replay` compares work RAM,
raw SRAM and picture hashes for existing recordings against `sawgod-headless --native all`, including
continuing from a save. The shipped binary's `--frames` mode uses the same setup as its SDL front end.
