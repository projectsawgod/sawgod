#!/usr/bin/env bash
# One command per release (Linux/Windows from Linux, macOS on a Mac); no ROM data.
set -euo pipefail
SECONDS=0
R=$(cd "$(dirname "$0")/.." && pwd)
. "$R/tools/env.sh"
export SAWGOD_REF
export SAWGOD_PACKAGE_JOBS=${SAWGOD_PACKAGE_JOBS:-4}
if [[ ! $SAWGOD_PACKAGE_JOBS =~ ^[1-4]$ ]]; then
  echo "SAWGOD_PACKAGE_JOBS must be between 1 and 4" >&2; exit 2
fi
target=${1:-}
flavor=${2:-both}
case "$flavor" in both|decomp) ;; *) echo "Flavor must be both or decomp" >&2; exit 2 ;; esac
if [[ "$target" == android ]]; then
  [[ "$flavor" == both ]] || { echo "The decomp front end is desktop-only" >&2; exit 2; }
  cd "$R"
  python3 "$R/tools/android_prepare.py"
  variant=${SAWGOD_ANDROID_VARIANT:-debug}
  case "$variant" in debug) task=assembleDebug ;; release) task=assembleRelease ;; *) exit 2 ;; esac
  (cd android && nice -n 10 env CMAKE_BUILD_PARALLEL_LEVEL="$SAWGOD_PACKAGE_JOBS" ./gradlew --no-daemon --max-workers=1 ":app:$task")
  apk="$R/android/app/build/outputs/apk/$variant/app-$variant.apk"
  "$ANDROID_HOME/build-tools/37.0.0/apksigner" verify "$apk"
  python3 "$R/tools/release/notices.py" --apk "$apk"
  mkdir -p "$R/dist"
  out="$R/dist/sawgod-android-arm64-v8a-$variant.apk"
  cp "$apk" "$out"
  python3 "$R/tools/name_scan.py" "$R/android/app/build/generated/sawgodAssets"
  (cd "$R/dist" && sha256sum "$(basename "$out")" > "$(basename "$out").sha256")
  python3 "$R/tools/name_scan.py" "$out"
  printf '%s\n' "$out"
  exit 0
fi
case "$target" in
  windows) preset=windows-mingw ;;
  linux) preset=linux-release ;;
  macos) preset=macos-release ;;
  *) echo 'Usage: tools/package.sh windows|linux|macos|android [both|decomp]' >&2; exit 2 ;;
esac
cd "$R"
jobs=$SAWGOD_PACKAGE_JOBS
build="$R/build/package-$target"
args=(--preset "$preset" -B "$build" -DSAWGOD_BUILD_TESTS=OFF
      -DSAWGOD_ENGLISH_FILL= -DSAWGOD_DEV_SOUNDFONT=
      "-DSAWGOD_RELEASE_VERSION=${SAWGOD_RELEASE_VERSION:-}")
# Optional local mirrors for disconnected builds. Otherwise CMake fetches the pinned upstream tags.
for dep in sdl3 miniz imgui lzma7z; do
  mirror="$R/ref/deps/$dep-src"
  [[ -d "$mirror" ]] || mirror="$R/build/deps/$dep-src"
  if [[ -d "$mirror" ]]; then
    dep_upper=$(printf '%s' "$dep" | tr '[:lower:]' '[:upper:]')
    args+=("-DFETCHCONTENT_SOURCE_DIR_$dep_upper=$mirror")
  fi
done
if [[ "$target" == linux ]]; then
  args+=('-DCMAKE_EXE_LINKER_FLAGS=-static-libgcc -static-libstdc++'
         '-DCMAKE_BUILD_RPATH=$ORIGIN/lib')
fi
if [[ "$target" == macos ]]; then
  # Reuse the same release build tested by CI before packaging.
  args+=(-DSAWGOD_BUILD_TESTS=ON)
fi
nice -n 10 cmake "${args[@]}"
if [[ "$flavor" == decomp ]]; then
  nice -n 10 cmake --build "$build" --target sawgod-decomp --parallel "$jobs"
  python3 "$R/tools/release/package_decomp.py" "$target" "$build"
  case "$target" in
    linux) decomp_name=sawgod-decomp-linux-x86_64; extension=tar.gz ;;
    windows) decomp_name=sawgod-decomp-windows-x86_64; extension=zip ;;
    macos) decomp_name=sawgod-decomp-macos-arm64; extension=zip ;;
  esac
  python3 "$R/tools/name_scan.py" "$R/dist/$decomp_name" "$R/dist/$decomp_name.$extension"
  exit 0
fi
nice -n 10 cmake --build "$build" --target sawgod sawgod-decomp --parallel "$jobs"
nice -n 10 "$R/tools/release/build_fluidsynth.sh" "$target" "$R/ref/fluidsynth" "$build/fluidsynth"
if [[ "$target" == macos ]]; then
  python3 "$R/tools/release/package_macos.py" "$build"
else
  python3 "$R/tools/release/package.py" "$target" "$build"
fi
python3 "$R/tools/release/package_decomp.py" "$target" "$build"
case "$target" in
  windows) package_name=sawgod-windows-x86_64; extension=zip ;;
  linux) package_name=sawgod-linux-x86_64; extension=tar.gz ;;
  macos) package_name=sawgod-macos-arm64; extension=zip ;;
esac
python3 "$R/tools/name_scan.py" "$R/dist/$package_name" "$R/dist/$package_name.$extension"
decomp_name=${package_name/sawgod-/sawgod-decomp-}
python3 "$R/tools/name_scan.py" "$R/dist/$decomp_name" "$R/dist/$decomp_name.$extension"
printf 'Package completed in %s seconds.\n' "$SECONDS"
