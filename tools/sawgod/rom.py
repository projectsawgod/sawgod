"""Known ROMs and hash-based identification.

The repo never contains ROM bytes; tools find the user's ROM by path and check it here.
"""
import hashlib
import zlib
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class KnownRom:
    key: str
    title: str
    size: int
    crc32: int
    sha1: str
    role: str


KNOWN_ROMS = (
    KnownRom("wsc-jp", "Makai Toushi Sa-Ga (Japan).wsc", 4194304, 0x1B6F5F30,
             "6b7a9b810c932974cbda4de7178cf0ea5a90e628", "baseline"),
    KnownRom("wsc-jp-tsaga13", "Makai Toushi Sa-Ga (Japan).wsc + Tower Reversed v1.3", 4194304, 0xFF4BAA20,
             "84c4b7138dfbcc44845fb9c21f58124f3264cb9d", "translation import"),
    KnownRom("gb-cos", "Makai Toushi Sa-Ga (World) (Ja) (Rev 1) (Collection of SaGa).gb", 131072, 0x0006612D,
             "3ce39646bffe38a9c8fd4913f0e950bae061d094", "authoritative GB"),
    KnownRom("gb-jp-rev1", "Makai Toushi Sa-Ga (Japan) (Rev 1).gb", 131072, 0x1953820F,
             "cbf480bc92bd98bae4fb79294b604d341fe58cbe", "GB reference"),
    KnownRom("gb-jp", "Makai Toushi Sa-Ga (Japan).gb", 131072, 0x131B09F2,
             "7a8f770a34207fee324431c47836f1ab317863f9", "GB reference"),
    KnownRom("gb-us", "Final Fantasy Legend, The (USA).gb", 131072, 0x8046148F,
             "901dfc83c72e172d35a376835807fc788444a9bb", "GB English reference"),
)

BASELINE = KNOWN_ROMS[0]


def identify_bytes(data: bytes):
    """Return the KnownRom matching data, or None."""
    sha1 = hashlib.sha1(data).hexdigest()
    for rom in KNOWN_ROMS:
        if rom.sha1 == sha1:
            return rom
    return None


def identify(path: Path):
    return identify_bytes(Path(path).read_bytes())


def load_baseline(path: Path) -> bytes:
    """Read the baseline WSC ROM, failing loudly on anything else."""
    data = Path(path).read_bytes()
    rom = identify_bytes(data)
    if rom is not BASELINE:
        raise SystemExit(f"{path}: not the baseline ROM (CRC32 {zlib.crc32(data):08X}); "
                         f"expected {BASELINE.title}, SHA-1 {BASELINE.sha1}")
    return data
