#!/usr/bin/env bash
# Offline native cross-build, independent of Gradle. Stage sources with android_prepare.py first.
set -euo pipefail
R=$(cd "$(dirname "$0")/.." && pwd)
. "$R/tools/env.sh"
abi=${1:-arm64-v8a}
case "$abi" in arm64-v8a|x86_64) ;; *) echo 'Expected arm64-v8a or x86_64' >&2; exit 2 ;; esac
build="$R/build/android-$abi"
args=(-S "$R" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release
      "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
      "-DANDROID_ABI=$abi" -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared
      -DSAWGOD_BUILD_TESTS=OFF -DSAWGOD_DEV_SOUNDFONT=)
for dep in sdl3 miniz imgui lzma7z; do
  args+=("-DFETCHCONTENT_SOURCE_DIR_${dep^^}=$R/build/deps/$dep-src")
done
android_cmake="$ANDROID_HOME/cmake/4.1.2/bin/cmake"
nice -n 10 "$android_cmake" "${args[@]}"
nice -n 10 "$android_cmake" --build "$build" --target sawgod --parallel 4
