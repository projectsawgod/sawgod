#!/usr/bin/env bash
# Rebuild the replaceable Android FluidSynth library from source/ inside the APK (unzip it first).
set -euo pipefail
sources=$(cd "${1:?source directory}" && pwd)
build=${2:?build directory}
abi=${3:-arm64-v8a}
: "${ANDROID_NDK_HOME:?point to NDK r29}"
mkdir -p "$build/project"
if [[ ! -d "$build/fluidsynth-2.6.1" ]]; then
  archive="$sources/fluidsynth-2.6.1.tgz"  # the APK's name; a source checkout's deps use .tar.gz
  [[ -f "$archive" ]] || archive="$sources/fluidsynth-2.6.1.tar.gz"
  tar -xzf "$archive" -C "$build"
fi
python3 - "$sources/gcem.zip" "$build" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as archive:
    archive.extractall(sys.argv[2])
PY
cp "$sources/AndroidFluidSynth.cmake" "$build/project/"
cat > "$build/project/CMakeLists.txt" <<'CMAKE'
cmake_minimum_required(VERSION 3.24)
project(rebuild_fluidsynth C CXX)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(AndroidFluidSynth.cmake)
CMAKE
build=$(cd "$build" && pwd)
nice -n 10 cmake -S "$build/project" -B "$build/native" -G Ninja \
  "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  "-DANDROID_ABI=$abi" -DANDROID_PLATFORM=android-26 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
  "-DSAWGOD_FLUIDSYNTH_SOURCE=$build/fluidsynth-2.6.1" \
  "-DGCEM_INCLUDE_DIR=$build/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200/include"
nice -n 10 cmake --build "$build/native" --target libfluidsynth --parallel 4
