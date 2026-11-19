# Per-OS paths for the build/test scripts (Windows Git Bash, Linux or macOS). Set R to the
# repo root, then source it:
#   R=$(cd "$(dirname "$0")/.." && pwd); . "$R/tools/env.sh"
# The Windows E:\ drive is the same shared drive as <shared-drive> on Linux.
# tools/paths.py is the Python twin of this file; keep the two in step.
# Every variable can be overridden from the environment.

SAWGOD_REF=${SAWGOD_REF:-$R/ref}
SAWGOD_ROM=${SAWGOD_ROM:-$SAWGOD_REF/rom/Makai Toushi Sa-Ga (Japan).wsc}
SAWGOD_TMP=${SAWGOD_TMP:-$R/tmp}
GHIDRA_HOME=${GHIDRA_HOME:-$R/tools/ghidra/ghidra_12.1.4_PUBLIC}
# Host-side Android tools, not the device's internal data directory (use adb to access that).
ANDROID_HOME=${ANDROID_HOME:-$SAWGOD_REF/android/sdk}
ANDROID_NDK_HOME=${ANDROID_NDK_HOME:-$ANDROID_HOME/ndk/29.0.14206865}
export ANDROID_HOME ANDROID_NDK_HOME

if [ "$(uname -s)" = Linux ] || [ "$(uname -s)" = Darwin ]; then
  SAWGOD_PY=${SAWGOD_PY:-$R/tools/.venv/bin/python}
  # Separate build trees so a Linux build never clobbers the Windows one.
  if [ "$(uname -s)" = Darwin ]; then
    SAWGOD_BUILD=${SAWGOD_BUILD:-$R/build/macos-release}
  else
    SAWGOD_BUILD=${SAWGOD_BUILD:-$R/build/linux-debug}
  fi
  winpath() { printf '%s\n' "$1"; }
else
  SAWGOD_PY=${SAWGOD_PY:-$R/tools/.venv/Scripts/python.exe}
  SAWGOD_BUILD=${SAWGOD_BUILD:-$R/build/windows-msvc}
  export PATH="/c/msys64/usr/bin:/c/msys64/mingw64/bin:$PATH"
  winpath() { cygpath -w "$1"; }
fi
