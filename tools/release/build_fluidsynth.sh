#!/usr/bin/env bash
# Also shipped in source/: rebuild the replaceable FluidSynth library with these exact options.
set -euo pipefail
target=${1:?windows, linux or macos}
sources=${2:?directory containing fluidsynth-2.6.1.tar.gz and gcem.zip}
build=${3:?build directory}
case "$target" in windows|linux|macos) ;; *) exit 2 ;; esac
mkdir -p "$sources" "$build"
fetch() {
  local name=$1 url=$2 hash=$3
  if [[ ! -f "$sources/$name" ]]; then
    if [[ -n ${SAWGOD_REF:-} && -f "$SAWGOD_REF/fluidsynth/$name" ]]; then
      cp "$SAWGOD_REF/fluidsynth/$name" "$sources/$name"
    elif [[ "$name" == gcem.zip && -n ${SAWGOD_REF:-} && -f "$SAWGOD_REF/fluidsynth/build/gcem.zip" ]]; then
      cp "$SAWGOD_REF/fluidsynth/build/gcem.zip" "$sources/$name"
    else
      curl --fail --location --retry 3 "$url" -o "$sources/$name.part"
      mv "$sources/$name.part" "$sources/$name"
    fi
  fi
  python3 - "$sources/$name" "$hash" <<'PYHASH'
import hashlib, pathlib, sys
if hashlib.sha256(pathlib.Path(sys.argv[1]).read_bytes()).hexdigest() != sys.argv[2]:
    raise SystemExit('Source archive checksum mismatch: ' + sys.argv[1])
PYHASH
}
fetch fluidsynth-2.6.1.tar.gz https://github.com/FluidSynth/fluidsynth/archive/refs/tags/v2.6.1.tar.gz \
  3d258a3bf97cc20c59eeebfe62c2432fae88adda74d3ad098681c76e0ebf446b
fetch gcem.zip https://github.com/kthohr/gcem/archive/012ae73c6d0a2cb09ffe86475f5c6fba3926e200.zip \
  28159274c54e9640354852e172d10d88eb159f4e7f2fea42edbcd20105ed3526
[[ ${4:-} != --fetch-only ]] || exit 0
if [[ ! -d "$sources/fluidsynth-2.6.1" ]]; then
  tar -xzf "$sources/fluidsynth-2.6.1.tar.gz" -C "$sources"
fi
if [[ ! -d "$sources/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200" ]]; then
  python3 - "$sources" <<'PY'
from pathlib import Path
import sys, zipfile
root = Path(sys.argv[1])
with zipfile.ZipFile(root / 'gcem.zip') as archive:
    archive.extractall(root)
PY
fi
args=(-S "$sources/fluidsynth-2.6.1" -B "$build" -G Ninja
      -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON -Dosal=cpp11
      "-DGCEM_INCLUDE_DIR=$sources/gcem-012ae73c6d0a2cb09ffe86475f5c6fba3926e200/include"
      '-DCMAKE_SHARED_LINKER_FLAGS=-static-libgcc -static-libstdc++')
# Prefix maps also cover assertions compiled into FluidSynth's release build.
args+=("-DCMAKE_C_FLAGS=-ffile-prefix-map=$sources=. -ffile-prefix-map=$build=./build"
      "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$sources=. -ffile-prefix-map=$build=./build")
# SawGod renders samples itself. No audio drivers, optional DSP engines or other third-party runtimes.
for feature in alsa aufile dbus jack ladspa libsndfile midishare network oss dsound wasapi waveout \
               winmidi sdl3 pulseaudio pipewire readline openmp systemd native-dls signalsmith; do
  args+=("-Denable-$feature=OFF")
done
if [[ "$target" == windows ]]; then
  args+=(-DCMAKE_SYSTEM_NAME=Windows -DCMAKE_SYSTEM_PROCESSOR=x86_64
         -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc-posix
         -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++-posix
         '-DCMAKE_SHARED_LINKER_FLAGS=-static -static-libgcc -static-libstdc++')
fi
if [[ "$target" == macos ]]; then
  args+=(-DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=11.0
         -DCMAKE_SHARED_LINKER_FLAGS=-Wl,-headerpad_max_install_names -Denable-framework=OFF
         -Denable-coreaudio=OFF -Denable-coremidi=OFF)
fi
cmake "${args[@]}"
cmake --build "$build" --target libfluidsynth --parallel "${SAWGOD_PACKAGE_JOBS:-4}"
