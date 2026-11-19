"""paths.py - per-OS locations of things the tools need (Windows, Linux or macOS).

The Windows E:\\ drive is the same shared drive as <shared-drive> on Linux.
tools/env.sh is the shell twin of this file; keep the two in step.
Each path can be overridden with the environment variable of the same name.
"""
import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WINDOWS = os.name == "nt"


def _env(name, default):
    return Path(os.environ.get(name) or default)


SAWGOD_REF = _env("SAWGOD_REF", ROOT / "ref")
SAWGOD_ROM = _env("SAWGOD_ROM", SAWGOD_REF / "rom" / "Makai Toushi Sa-Ga (Japan).wsc")
SAWGOD_TMP = _env("SAWGOD_TMP", ROOT / "tmp")
GHIDRA_HOME = _env("GHIDRA_HOME", ROOT / "tools" / "ghidra" / "ghidra_12.1.4_PUBLIC")
# Android tools run on the host. Device data stays in SDL_GetPrefPath's internal files dir;
# SAWGOD_HOME can point at a copy pulled with adb, never a guessed desktop path on the device.
ANDROID_HOME = _env("ANDROID_HOME", SAWGOD_REF / "android/sdk")
ANDROID_NDK_HOME = _env("ANDROID_NDK_HOME", ANDROID_HOME / "ndk/29.0.14206865")
# Separate build trees so a Linux build never clobbers the Windows one.
SAWGOD_BUILD = _env("SAWGOD_BUILD", ROOT / "build" / ("windows-msvc" if WINDOWS else "macos-release" if sys.platform == "darwin" else "linux-debug"))
# The game's data folder (src/engine/data_dir.h): config, saves, the ROM copy, the .sgz archives, mods/.
# $SAWGOD_HOME when set; else the OS folder (SDL_GetPrefPath("sawgod", "sawgod")), which development builds
# (a CMake build tree) always use. A portable install keeps it beside the game instead: set SAWGOD_HOME there.
if WINDOWS:
    _user = Path(os.environ.get("APPDATA", Path.home())) / "sawgod" / "sawgod"
elif os.uname().sysname == "Darwin":
    _user = Path.home() / "Library" / "Application Support" / "sawgod" / "sawgod"
else:
    _user = Path(os.environ.get("XDG_DATA_HOME") or Path.home() / ".local" / "share") / "sawgod" / "sawgod"
SAWGOD_DATA = _env("SAWGOD_HOME", _user)


def resources():
    """The game's resources as a folder (tools read files; the game reads <data>/wsc.sgz and gb.sgz): both
    archives unpacked into tmp/resources/, refreshed when an archive is newer. Run the game once first."""
    import zipfile
    out = SAWGOD_TMP / "resources"
    for name in ("wsc.sgz", "gb.sgz"):
        archive = SAWGOD_DATA / name
        stamp = out / f".{name}.mtime"
        if not archive.exists():
            continue
        mtime = str(archive.stat().st_mtime_ns)
        if stamp.exists() and stamp.read_text(encoding="utf-8") == mtime:
            continue
        with zipfile.ZipFile(archive) as z:
            z.extractall(out)
        stamp.write_text(mtime, encoding="utf-8")
    return out
