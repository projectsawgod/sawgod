# Android

Phones use on-screen touch controls; handhelds can use their built-in gamepad. Package ID `org.sawgod.app`, arm64-v8a, landscape and immersive.
Minimum Android 8.0 (API 26): a practical floor for Vulkan-era handhelds, Java NIO file installation and SDL3.
Compile/target SDK 36, build tools 37.0.0, NDK r29 (29.0.14206865), CMake 4.1.2, Java 21.
Vulkan is optional in the manifest. Enhanced uses SDL3 GPU on Vulkan; creation/probe failure falls back to the
legacy (non-GPU) renderer (WSC/GB available, Enhanced graphics unavailable).

## Build

Host tools use `ANDROID_HOME` and `ANDROID_NDK_HOME` (defaults in tools/env.sh and tools/paths.py).
No SDK, library sources, soundfont, signing credentials or generated assets are checked into android/.
SDL's Java classes and Gradle wrapper are copied from our pinned SDL3 3.4.16 android-project template;
[AGP 8.10.1](https://developer.android.com/build/releases/agp-8-10-0-release-notes) supports SDK 36; the wrapper pins Gradle 8.12. SDL retains its zlib notices in each file.

Prepare the pinned sources at `ref/deps/{sdl3,miniz,imgui,lzma7z}-src` (same tags as CMakeLists.txt),
FluidSynth 2.6.1 and GCEM archives in `ref/fluidsynth/`, and GeneralUser GS 2.0.3 in
`ref/soundfonts/GeneralUser-GS.sf2`. The preparer copies sources into build/deps; it never writes ref/.
The preparer packages the English text, `assets/text/en.csv`.

```sh
python3 tools/android_prepare.py
# Offline NDK/CMake check (no Gradle, no network):
tools/android_native.sh arm64-v8a
# Optional emulator ABI, supported by the same project:
tools/android_native.sh x86_64
# Network needed on the first Gradle run:
cd android
nice -n 10 ./gradlew --no-daemon --max-workers=1 assembleDebug assembleRelease
# Optional emulator APK:
nice -n 10 ./gradlew --no-daemon --max-workers=1 -PsawgodAbis=x86_64 assembleDebug
```

Gradle also runs android_prepare.py before native configuration and asset merging. Native Ninja compile
and link pools are limited to four jobs; Gradle runs one ABI at a time. Invoke Gradle under nice.

`tools/package.sh android` prepares and builds a debug-signed APK, verifies its signature, and writes
`dist/sawgod-android-arm64-v8a-debug.apk` plus SHA-256. Set `SAWGOD_ANDROID_VARIANT=release` for the
optimized release variant; without release credentials it is explicitly **debug-signed for testing**.
Direct Gradle APKs are in `android/app/build/outputs/apk/{debug,release}/`.

Release signing uses environment variables: `SAWGOD_ANDROID_KEYSTORE` (absolute path),
`SAWGOD_ANDROID_STORE_PASSWORD`, `SAWGOD_ANDROID_KEY_ALIAS`, `SAWGOD_ANDROID_KEY_PASSWORD`.
Supply all four from a private shell/secret manager, outside the repo. Never commit a key or password.
Keep the release key safe: Android updates must use the same signature. A differently signed install
requires uninstalling the old app, which removes its internal data. The debug key is test-only.

## Storage and first run

On first launch press A to choose the ROM through Android's system document picker (SAF). The setup
screen accepts taps; touch controls appear once the game starts. Any provider/MIME type is allowed:
raw `.wsc`, `.zip` and `.7z` are recognized by their bytes. The exact supported SHA-1 is required.
The URI is opened with SDL IO, verified, and the ROM is copied atomically into internal `files/rom/rom.wsc`.
Archives are built there with the same extraction code as desktop. No storage permissions or persistent
URI grants are requested; moving/removing the original document after import does not affect the game.

SDL_GetPrefPath on Android is the app's internal files directory. It holds the ROM, wsc.sgz, gb.sgz,
saves, sawgod.toml and mods. Authored GB map inputs, English and the soundfont are installed there from
APK assets before native startup. The installed manifest is retained: upgrades copy only changed or
missing files and remove only previously listed bundled assets. Older digest stamps migrate by hashing
existing files instead of recopying them. Changed files are flushed and atomically renamed; affected directories are synced
before the manifest is atomically replaced. An unchanged launch writes nothing. The installer
never overwrites ROM data, archives, saves, config or mods. Uninstall/clear-data removes everything.
Licenses and dependency source archives remain in APK assets. Backups are disabled to avoid copying ROM
content into cloud backups. A debug build's files can be inspected with `adb shell run-as org.sawgod.app`.

## Touch controls

The Gamepad page starts with the saved **Touch Controls: Auto / On / Off** setting
(`input.touch_controls` in `sawgod.toml`, default Auto). Auto shows the overlay when no real controller
is connected; connecting one hides it, and disconnecting the last brings it back immediately without
pausing. Unmapped joysticks and the overlay's own virtual joystick do not count as controllers.
On always shows the overlay, including alongside a handheld's built-in pad. Off hides it completely.
Real controllers and the keyboard always work alongside touch, regardless of this setting; controller
connections never change a saved choice.

The fixed landscape layout puts two split pads in the left bar (faint Y above X), A above/right of B
in the right bar, Options (hamburger) and Mode (upward double chevron) at top left, and START at top right.
Every control uses its reference size scaled by window height: pads 320 px, A/B 172 px,
Options/Mode 120 px, and START 240x120 px at 1080 high. Physical floors are 57 dp (about 9 mm)
for pads/A/B and 48 dp for the top row. Controls never shrink to fit side bars; they move inward
as needed to keep their hit areas inside the safe area and may overlap the picture. Touch on/off
uses the same normal fit-to-window picture and Options geometry, without whole-pixel snapping.
Multiple fingers can hold directions and buttons together. Pads have center and diagonal dead zones,
and sliding changes direction without lifting. Button hit areas extend 20% beyond their drawn radius.
There is no layout editor or haptics.

X drives the default d-pad bindings, Y drives the right stick, and A/B/START drive their default gamepad
buttons through an SDL virtual gamepad. Rebinding, Swap A/B, and button prompts use the existing input path.
Options opens the same menu as the Options binding; the menu accepts taps directly. Mode cycles unlocked
tracks **GB -> WSC -> Enhanced -> GB**, with the usual fade. Locked graphics/music stay locked; Mode
Down/Up bindings still stop at the ends. The overlay hides and releases held buttons on the ROM screen,
on background/focus loss, or whenever the setting hides it.
While Options is open, the hamburger, X pad, A and B stay visible and active: close, navigate, accept
and back through the normal gamepad menu path. Y, Mode and START are hidden and inactive. Entering or
leaving Options releases held touch inputs. Binding capture ignores the touch joystick; the hamburger
cancels capture. Android Back performs the same Options action (or opens Options in game), even with
Touch Controls Off, without quitting.

Desktop builds hide the Touch Controls row. Launch with `--touch-controls` to show the same saved
setting and enable mouse testing of the overlay. Whole-pixel picture scaling follows overlay visibility.
Button prompts follow the last device pressed; Auto icons prefer a real controller's family over touch.

## FluidSynth and fibers

FluidSynth is a separate dynamically loaded `libfluidsynth.so`, with shared libc++. It uses the C++11 OS
layer (no GLib) and GCEM headers. Disabled: ALSA, audio-file output, DBus, JACK, LADSPA, libsndfile,
MidiShare, network, OSS, DirectSound, WASAPI, WaveOut, WinMIDI, SDL audio, PulseAudio, PipeWire, readline,
OpenMP, systemd, native DLS, Signalsmith, PortAudio, OpenSL ES and Oboe. SDL motion sensors are disabled for this gamepad build; miniz uses memory IO only. Private desktop
GPU timestamp instrumentation is omitted from Android; rendering/effects are unchanged.
The app sends the existing mixer
samples to SDL; ordinary FluidSynth chorus/reverb and threading remain enabled. No Enhanced casting changes.

The APK includes exact FluidSynth/GCEM source archives and a rebuild script in `assets/source/`, plus
licenses/. To replace FluidSynth, unzip the APK's assets/source into a writable folder, then run:

```sh
bash build_android_fluidsynth.sh "$PWD" "$PWD/rebuild" arm64-v8a
```

Replace `lib/arm64-v8a/libfluidsynth.so` in an unpacked APK with
`rebuild/native/fluidsynth/src/libfluidsynth.so`, zipalign and sign with your own key (install under that
signature), or rebuild the app from source. Modifying/replacing the library and reverse engineering
SawGod to debug those modifications is permitted under LGPL-2.1 section 6. No proprietary signing key
is required to run a modified build. Keep libc++_shared.so alongside the libraries.

Bionic lacks ucontext functions. The game therefore uses a small cooperative ABI context switch on
arm64/x86_64, keeping the same stackful fiber/yield/cancel semantics and unwinding destructors on cancel.
It saves callee-saved registers, stack pointer and floating-point control state, on the same thread.

## Device smoke check

```sh
adb install -r android/app/build/outputs/apk/debug/app-debug.apk
adb logcat -c
adb shell am start -n org.sawgod.app/.SawGodActivity
adb logcat -s SDL SDL/APP AndroidRuntime
# Restart without clearing saves or the imported ROM:
adb shell am force-stop org.sawgod.app
adb shell am start -n org.sawgod.app/.SawGodActivity
adb shell run-as org.sawgod.app ls files
```

First install: choose the supported ROM, see extraction finish, reach the title. Cancel the picker and
retry; choose an invalid file and retry. Test a raw ROM and an archive, preferably from two providers.
WSC, GB and Enhanced: correct picture/chrome, gamepad movement/confirm/cancel, Options (Select button), Swap
(L1/R1) and fast-forward (R2). Listen for WSC, GB and Enhanced music/SFX, switch modes, background/resume.
Save in two slots, force-stop/restart, confirm saves and config survive and no picker returns. Rotate
within landscape, sleep/wake and reconnect a controller. On Thor use its top display at 120 Hz and check
pacing, latency, audio crackle, heat and battery feel in battles and world-map Enhanced. Native logic
runs at its own rate; 120 Hz changes presentation only. Check the legacy fallback separately
with a debug config selecting legacy rendering. Emulator GPU/audio are smoke evidence only, not Thor
performance evidence.


## GitHub release signing and SDK cache

CI pins command-line tools `14742923` and installs `platform-tools`, `platforms;android-36`, `build-tools;37.0.0`,
`ndk;29.0.14206865` and `cmake;4.1.2` with sdkmanager, matching the recorded reference
versions. The SDK/NDK/CMake directories, pinned source inputs and Gradle caches are cached;
no ROM, extracted assets or signing key is cached. Java is Temurin 21. Platform-tools may
receive upstream revisions; the compile/target SDK and native toolchain stay pinned.

Configure exactly four repository Actions secrets. Only tag (release) builds receive them; pushes and pull
requests build debug-signed APKs:

- `SAWGOD_ANDROID_KEYSTORE_BASE64`: base64 encoding of the release keystore file (not a path).
- `SAWGOD_ANDROID_STORE_PASSWORD`: keystore password.
- `SAWGOD_ANDROID_KEY_ALIAS`: alias inside the keystore.
- `SAWGOD_ANDROID_KEY_PASSWORD`: key password.

The job decodes the key into runner temporary storage, exports the four signing environment
variables documented above, builds with `SAWGOD_ANDROID_VARIANT=release`, verifies the APK
signature and removes the temporary keystore on exit. Partial credentials fail; with no secrets,
Gradle uses the debug key and the draft release body explicitly says **TESTING BUILD**.
The APK filename remains `sawgod-android-arm64-v8a-release.apk` in either case. Keep using the
same release key for updates. Never upload a key or its passwords as an artifact.

`assets/README.txt`, `assets/THIRD_PARTY_NOTICES.txt`, `assets/licenses/` and `assets/source/`
are mandatory package contents. `tools/android_prepare.py` audits staging; `tools/package.sh`
also audits the final APK, including the native-library inventory. Source archives and replacement
instructions are included even in a release-key-signed APK; users may rebuild and sign modified
versions with their own keys. See [RELEASE.md](RELEASE.md) for the tag/dispatch workflow.
